#include "gravel/datasets/net_gtfs.h"

#include "gravel/core/array_graph.h"
#include "gravel/core/types.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gravel {

namespace {

// ---------------------------------------------------------------------------
// RFC 4180 CSV reader (quote-aware, BOM-stripping, header-mapped).
//
// GTFS mandates RFC 4180 CSV: fields with commas / quotes / newlines are
// double-quoted, embedded quotes are doubled (""), lines end in \n or \r\n, a
// UTF-8 BOM may prefix the first header, column order is not fixed, and unknown
// columns must be ignored. A naive split(',') is wrong — so we parse properly.
// ---------------------------------------------------------------------------

/// Read one RFC 4180 record from `in`, honoring quoted fields that may span
/// physical lines. Returns false at end of file with no more records.
bool read_csv_record(std::istream& in, std::vector<std::string>& out) {
    out.clear();
    std::string field;
    bool in_quotes = false;
    bool saw_any = false;
    int c;
    while ((c = in.get()) != EOF) {
        saw_any = true;
        char ch = static_cast<char>(c);
        if (in_quotes) {
            if (ch == '"') {
                int n = in.peek();
                if (n == '"') {  // doubled quote -> literal quote
                    in.get();
                    field.push_back('"');
                } else {
                    in_quotes = false;
                }
            } else {
                field.push_back(ch);
            }
        } else {
            if (ch == '"') {
                in_quotes = true;
            } else if (ch == ',') {
                out.push_back(std::move(field));
                field.clear();
            } else if (ch == '\r') {
                // swallow; \n (if any) ends the record
            } else if (ch == '\n') {
                out.push_back(std::move(field));
                return true;
            } else {
                field.push_back(ch);
            }
        }
    }
    if (!saw_any) return false;  // clean EOF, nothing buffered
    out.push_back(std::move(field));  // final record without trailing newline
    return true;
}

/// Strip a leading UTF-8 BOM (EF BB BF) from the first header cell if present.
void strip_bom(std::string& s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
}

/// A parsed CSV table: header→column-index map plus row data. Small GTFS tables
/// (stops, routes, trips, frequencies) are loaded fully; only rows with enough
/// fields are kept. Missing files are reported by the caller.
struct CsvTable {
    std::unordered_map<std::string, int> col;  ///< header name -> column index
    std::vector<std::vector<std::string>> rows;

    /// Column index for `name`, or -1 if the header is absent.
    int index_of(const std::string& name) const {
        auto it = col.find(name);
        return it == col.end() ? -1 : it->second;
    }

    /// Field value at (row, index), or "" if the index is -1 or out of range.
    static const std::string& at(const std::vector<std::string>& row, int idx) {
        static const std::string empty;
        if (idx < 0 || idx >= static_cast<int>(row.size())) return empty;
        return row[idx];
    }
};

CsvTable read_csv_table(const std::string& path, bool required) {
    CsvTable table;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (required) throw std::runtime_error("GTFS: cannot open required file: " + path);
        return table;  // optional file absent -> empty table
    }
    std::vector<std::string> header;
    if (!read_csv_record(in, header)) return table;  // empty file
    if (!header.empty()) strip_bom(header.front());
    for (int i = 0; i < static_cast<int>(header.size()); ++i) {
        table.col.emplace(header[i], i);
    }
    std::vector<std::string> row;
    while (read_csv_record(in, row)) {
        // Skip fully-blank trailing lines.
        if (row.size() == 1 && row[0].empty()) continue;
        table.rows.push_back(row);
    }
    return table;
}

// ---------------------------------------------------------------------------
// GTFS field parsing helpers.
// ---------------------------------------------------------------------------

/// Parse a decimal string; returns false (leaving `out` untouched) on empty /
/// malformed input rather than throwing, so a single bad row cannot abort a feed.
bool parse_double(const std::string& s, double& out) {
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos == 0) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_long(const std::string& s, long& out) {
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        long v = std::stol(s, &pos);
        if (pos == 0) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

/// Parse a GTFS time `H:MM:SS` / `HH:MM:SS` into total seconds from service-day
/// start. Values may exceed 24:00:00 (e.g. 25:30:00) — we never clamp or mod.
/// Returns false for empty / malformed (interpolated intermediate times are empty).
bool parse_gtfs_time(const std::string& s, long& seconds) {
    if (s.empty()) return false;
    long h = 0, m = 0, sec = 0;
    int part = 0;      // 0=H, 1=M, 2=S
    long cur = 0;
    bool any = false;
    for (char ch : s) {
        if (ch == ':') {
            if (part == 0) h = cur;
            else if (part == 1) m = cur;
            ++part;
            cur = 0;
        } else if (ch >= '0' && ch <= '9') {
            cur = cur * 10 + (ch - '0');
            any = true;
        } else {
            return false;  // unexpected character
        }
    }
    if (!any || part != 2) return false;  // must have exactly two colons
    sec = cur;
    seconds = 3600 * h + 60 * m + sec;
    return true;
}

/// Map a GTFS `route_type` to a per-mode vehicle capacity from the model.
/// Handles the base enum and the extended (>=100) route-type codes by hundreds
/// family; anything unrecognized falls back to bus-like `other`.
double vehicle_capacity(long route_type, const GtfsCapacityModel& m) {
    long base = route_type;
    if (route_type >= 100) base = route_type / 100;  // extended-code family
    switch (base) {
        case 3:
        case 11: return m.bus;
        case 0:
        case 5:
        case 12: return m.tram;
        case 1: return m.subway;
        case 2: return m.rail;
        case 4: return m.ferry;
        case 6:
        case 7: return m.lift;
        default: return m.other;
    }
}

// ---------------------------------------------------------------------------
// Per-hop accumulator. A directed edge is keyed by (from_node, to_node); many
// trips traverse the same physical hop, so we merge and accumulate.
// ---------------------------------------------------------------------------

struct HopAgg {
    NodeID from = 0;
    NodeID to = 0;
    double veh_per_hr = 0.0;      ///< summed vehicle frequency across routes on this hop
    double capacity = 0.0;        ///< summed persons-per-hour throughput proxy
    double traversal_sum = 0.0;   ///< sum of known stop-to-stop traversal seconds
    long traversal_n = 0;         ///< count of known traversals (for the mean)
};

/// Nominal traversal weight (seconds) when both stop times are interpolated/empty.
constexpr double kDefaultHopSeconds = 120.0;

}  // namespace

NetworkGraph load_gtfs_network(const GtfsConfig& config) {
    const std::string& dir = config.dir;
    const std::string sep = (!dir.empty() && dir.back() == '/') ? "" : "/";
    auto path = [&](const char* file) { return dir + sep + file; };

    // --- Load the small tables fully (required + optional frequencies). ---
    CsvTable stops = read_csv_table(path("stops.txt"), /*required=*/true);
    CsvTable routes = read_csv_table(path("routes.txt"), /*required=*/true);
    CsvTable trips = read_csv_table(path("trips.txt"), /*required=*/true);
    CsvTable freqs = read_csv_table(path("frequencies.txt"), /*required=*/false);

    // --- stops.txt -> node ids + coordinates (location_type 0 / empty only). ---
    const int s_id = stops.index_of("stop_id");
    const int s_lat = stops.index_of("stop_lat");
    const int s_lon = stops.index_of("stop_lon");
    const int s_loc = stops.index_of("location_type");
    if (s_id < 0) throw std::runtime_error("GTFS: stops.txt missing stop_id column");

    std::unordered_map<std::string, NodeID> node_of;  // stop_id -> dense node id
    std::vector<Coord> coords;
    node_of.reserve(stops.rows.size());
    coords.reserve(stops.rows.size());
    for (const auto& row : stops.rows) {
        const std::string& id = CsvTable::at(row, s_id);
        if (id.empty()) continue;
        const std::string& loc = CsvTable::at(row, s_loc);
        if (!loc.empty() && loc != "0") continue;  // structural stop, not boardable
        double lat = 0.0, lon = 0.0;
        parse_double(CsvTable::at(row, s_lat), lat);
        parse_double(CsvTable::at(row, s_lon), lon);
        auto [it, inserted] = node_of.emplace(id, static_cast<NodeID>(coords.size()));
        if (inserted) coords.push_back(Coord{lat, lon});
    }

    // --- routes.txt -> route_id -> route_type. ---
    const int r_id = routes.index_of("route_id");
    const int r_type = routes.index_of("route_type");
    std::unordered_map<std::string, long> route_type_of;
    if (r_id >= 0) {
        for (const auto& row : routes.rows) {
            const std::string& id = CsvTable::at(row, r_id);
            if (id.empty()) continue;
            long rt = 3;  // default bus-like if the value is missing/malformed
            parse_long(CsvTable::at(row, r_type), rt);
            route_type_of[id] = rt;
        }
    }

    // --- trips.txt -> trip_id -> route_id. ---
    const int t_trip = trips.index_of("trip_id");
    const int t_route = trips.index_of("route_id");
    std::unordered_map<std::string, std::string> route_of_trip;
    if (t_trip >= 0 && t_route >= 0) {
        for (const auto& row : trips.rows) {
            const std::string& tid = CsvTable::at(row, t_trip);
            if (tid.empty()) continue;
            route_of_trip[tid] = CsvTable::at(row, t_route);
        }
    }

    // --- frequencies.txt -> trip_id -> mean headway_secs across its windows. ---
    // A trip in frequencies.txt is headway-based: use headway directly rather than
    // counting its literal stop_times departures (which would double-count).
    std::unordered_map<std::string, double> headway_of_trip;
    {
        const int f_trip = freqs.index_of("trip_id");
        const int f_hw = freqs.index_of("headway_secs");
        if (f_trip >= 0 && f_hw >= 0) {
            std::unordered_map<std::string, std::pair<double, long>> acc;  // sum, count
            for (const auto& row : freqs.rows) {
                const std::string& tid = CsvTable::at(row, f_trip);
                if (tid.empty()) continue;
                long hw = 0;
                if (!parse_long(CsvTable::at(row, f_hw), hw) || hw <= 0) continue;
                auto& a = acc[tid];
                a.first += static_cast<double>(hw);
                a.second += 1;
            }
            for (const auto& [tid, a] : acc) {
                if (a.second > 0) headway_of_trip[tid] = a.first / static_cast<double>(a.second);
            }
        }
    }

    // --- Stream stop_times.txt, grouping consecutive rows by trip. ---
    // Rows within a trip must be ordered by stop_sequence; feeds are almost always
    // pre-sorted, but sequence numbers are increasing-not-contiguous, so we sort
    // each trip's rows numerically before emitting consecutive-pair edges.
    struct StRow {
        long seq = 0;
        NodeID node = INVALID_NODE;
        long arrival = -1;    // seconds, -1 = empty/interpolated
        long departure = -1;
    };

    std::ifstream st_in(path("stop_times.txt"), std::ios::binary);
    if (!st_in) throw std::runtime_error("GTFS: cannot open required file: " + path("stop_times.txt"));
    std::vector<std::string> header;
    if (!read_csv_record(st_in, header)) {
        throw std::runtime_error("GTFS: stop_times.txt is empty");
    }
    if (!header.empty()) strip_bom(header.front());
    std::unordered_map<std::string, int> st_col;
    for (int i = 0; i < static_cast<int>(header.size()); ++i) st_col.emplace(header[i], i);
    auto st_idx = [&](const char* n) {
        auto it = st_col.find(n);
        return it == st_col.end() ? -1 : it->second;
    };
    const int st_trip = st_idx("trip_id");
    const int st_stop = st_idx("stop_id");
    const int st_seq = st_idx("stop_sequence");
    const int st_arr = st_idx("arrival_time");
    const int st_dep = st_idx("departure_time");
    if (st_trip < 0 || st_stop < 0 || st_seq < 0) {
        throw std::runtime_error(
            "GTFS: stop_times.txt missing trip_id / stop_id / stop_sequence");
    }

    // Bucket all rows by trip_id (a first pass; feeds are large but this test-scale
    // parser favors correctness and simplicity over streaming the biggest file).
    std::unordered_map<std::string, std::vector<StRow>> by_trip;
    {
        std::vector<std::string> row;
        auto field = [&](const std::vector<std::string>& r, int idx) -> const std::string& {
            static const std::string empty;
            if (idx < 0 || idx >= static_cast<int>(r.size())) return empty;
            return r[idx];
        };
        while (read_csv_record(st_in, row)) {
            if (row.size() == 1 && row[0].empty()) continue;
            const std::string& tid = field(row, st_trip);
            const std::string& sid = field(row, st_stop);
            if (tid.empty() || sid.empty()) continue;
            auto node_it = node_of.find(sid);
            if (node_it == node_of.end()) continue;  // stop not a boardable node
            StRow sr;
            long seq = 0;
            parse_long(field(row, st_seq), seq);
            sr.seq = seq;
            sr.node = node_it->second;
            parse_gtfs_time(field(row, st_arr), sr.arrival);
            parse_gtfs_time(field(row, st_dep), sr.departure);
            by_trip[tid].push_back(sr);
        }
    }

    // --- Emit consecutive-pair edges, aggregating per (from, to) hop. ---
    std::unordered_map<uint64_t, HopAgg> hops;  // key = (from<<32)|to
    const double win_hours = (config.window_hours > 0.0) ? config.window_hours : 18.0;

    for (auto& [tid, rows] : by_trip) {
        std::sort(rows.begin(), rows.end(),
                  [](const StRow& a, const StRow& b) { return a.seq < b.seq; });

        // Resolve this trip's route_type and its vehicle frequency contribution.
        long route_type = 3;  // bus-like default
        auto rt_it = route_of_trip.find(tid);
        if (rt_it != route_of_trip.end()) {
            auto ty_it = route_type_of.find(rt_it->second);
            if (ty_it != route_type_of.end()) route_type = ty_it->second;
        }
        const double cap_per_veh = vehicle_capacity(route_type, config.capacity_model);

        // Vehicle frequency (veh/hr) this trip contributes to each hop it traverses.
        // Headway-based trips: 3600 / headway_secs. Scheduled trips: one vehicle
        // over the analysis window -> 1 / window_hours.
        double veh_per_hr;
        auto hw_it = headway_of_trip.find(tid);
        if (hw_it != headway_of_trip.end() && hw_it->second > 0.0) {
            veh_per_hr = 3600.0 / hw_it->second;
        } else {
            veh_per_hr = 1.0 / win_hours;
        }

        for (size_t i = 0; i + 1 < rows.size(); ++i) {
            const StRow& a = rows[i];
            const StRow& b = rows[i + 1];
            if (a.node == b.node) continue;  // no self-loops from repeated stops
            uint64_t key = (static_cast<uint64_t>(a.node) << 32) | static_cast<uint64_t>(b.node);
            HopAgg& h = hops[key];
            h.from = a.node;
            h.to = b.node;
            h.veh_per_hr += veh_per_hr;
            h.capacity += veh_per_hr * cap_per_veh;
            if (a.departure >= 0 && b.arrival >= 0 && b.arrival >= a.departure) {
                h.traversal_sum += static_cast<double>(b.arrival - a.departure);
                h.traversal_n += 1;
            }
        }
    }

    // --- Build CSR arrays manually so `capacity` stays aligned to CSR edge order. ---
    const NodeID num_nodes = static_cast<NodeID>(coords.size());
    std::vector<uint32_t> offsets(num_nodes + 1, 0);
    for (const auto& [key, h] : hops) offsets[h.from + 1]++;
    for (NodeID i = 1; i <= num_nodes; ++i) offsets[i] += offsets[i - 1];

    const size_t edge_count = hops.size();
    std::vector<NodeID> targets(edge_count);
    std::vector<Weight> weights(edge_count);
    std::vector<double> capacity(edge_count);
    auto pos = offsets;
    for (const auto& [key, h] : hops) {
        uint32_t idx = pos[h.from]++;
        targets[idx] = h.to;
        weights[idx] = (h.traversal_n > 0)
                           ? h.traversal_sum / static_cast<double>(h.traversal_n)
                           : kDefaultHopSeconds;
        capacity[idx] = h.capacity;
    }

    NetworkGraph result;
    result.graph = std::make_unique<ArrayGraph>(
        std::move(offsets), std::move(targets), std::move(weights), std::move(coords));
    result.capacity = std::move(capacity);
    return result;
}

}  // namespace gravel
