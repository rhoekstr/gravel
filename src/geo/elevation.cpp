#include "gravel/geo/elevation.h"
#include "gravel/io/binary_format.h"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <utility>

namespace gravel {

double ElevationData::edge_max_elevation(NodeID u, NodeID v) const {
    if (u >= node_elevation.size() || v >= node_elevation.size()) return NAN;
    double eu = node_elevation[u];
    double ev = node_elevation[v];
    if (std::isnan(eu) && std::isnan(ev)) return NAN;
    if (std::isnan(eu)) return ev;
    if (std::isnan(ev)) return eu;
    return std::max(eu, ev);
}

bool ElevationData::has_elevation(NodeID node) const {
    return node < node_elevation.size() && !std::isnan(node_elevation[node]);
}

// SRTM HGT format: 1201x1201 (3-arc-second) or 3601x3601 (1-arc-second)
// big-endian int16_t, row-major from NW corner
namespace {

struct HGTTile {
    int lat;  // SW corner latitude
    int lon;  // SW corner longitude
    uint32_t samples_per_side;
    std::vector<int16_t> data;

    double interpolate(double lat_frac, double lon_frac) const {
        // lat_frac, lon_frac are offsets within the tile [0, 1)
        double row_d = (1.0 - lat_frac) * (samples_per_side - 1);
        double col_d = lon_frac * (samples_per_side - 1);

        uint32_t r0 = static_cast<uint32_t>(row_d);
        uint32_t c0 = static_cast<uint32_t>(col_d);
        uint32_t r1 = std::min(r0 + 1, samples_per_side - 1);
        uint32_t c1 = std::min(c0 + 1, samples_per_side - 1);

        double dr = row_d - r0;
        double dc = col_d - c0;

        auto get = [&](uint32_t r, uint32_t c) -> double {
            int16_t v = data[r * samples_per_side + c];
            if (v == -32768) return NAN;  // void
            return static_cast<double>(v);
        };

        double v00 = get(r0, c0);
        double v01 = get(r0, c1);
        double v10 = get(r1, c0);
        double v11 = get(r1, c1);

        // Bilinear interpolation (skip NaN voids)
        if (std::isnan(v00) || std::isnan(v01) || std::isnan(v10) || std::isnan(v11)) {
            // Return nearest non-NaN
            if (!std::isnan(v00)) return v00;
            if (!std::isnan(v01)) return v01;
            if (!std::isnan(v10)) return v10;
            if (!std::isnan(v11)) return v11;
            return NAN;
        }

        return (1.0 - dr) * ((1.0 - dc) * v00 + dc * v01) +
               dr * ((1.0 - dc) * v10 + dc * v11);
    }
};

std::string tile_filename(int lat, int lon) {
    char buf[16];
    char ns = lat >= 0 ? 'N' : 'S';
    char ew = lon >= 0 ? 'E' : 'W';
    std::snprintf(buf, sizeof(buf), "%c%02d%c%03d.hgt",
                  ns, std::abs(lat), ew, std::abs(lon));
    return std::string(buf);
}

HGTTile load_hgt(const std::string& path, int lat, int lon) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Cannot open HGT file: " + path);

    auto size = f.tellg();
    f.seekg(0);

    HGTTile tile;
    tile.lat = lat;
    tile.lon = lon;

    // Determine resolution from file size
    if (size == 1201 * 1201 * 2) {
        tile.samples_per_side = 1201;  // SRTM3 (3-arc-second)
    } else if (size == 3601 * 3601 * 2) {
        tile.samples_per_side = 3601;  // SRTM1 (1-arc-second)
    } else {
        throw std::runtime_error("Unexpected HGT file size: " + path);
    }

    uint32_t n = tile.samples_per_side * tile.samples_per_side;
    tile.data.resize(n);

    // Read big-endian int16_t
    std::vector<uint8_t> raw(n * 2);
    f.read(reinterpret_cast<char*>(raw.data()), raw.size());

    for (uint32_t i = 0; i < n; ++i) {
        tile.data[i] = static_cast<int16_t>((raw[i * 2] << 8) | raw[i * 2 + 1]);
    }

    return tile;
}

}  // namespace

ElevationData load_srtm_elevation(const ArrayGraph& graph, const std::string& srtm_dir) {
    ElevationData result;
    NodeID n = graph.node_count();
    result.node_elevation.assign(n, NAN);

    // Cache loaded tiles
    std::unordered_map<int64_t, HGTTile> tile_cache;
    auto tile_key = [](int lat, int lon) -> int64_t {
        return static_cast<int64_t>(lat) * 1000 + lon;
    };

    for (NodeID v = 0; v < n; ++v) {
        auto coord = graph.node_coordinate(v);
        if (!coord) continue;

        int tile_lat = static_cast<int>(std::floor(coord->lat));
        int tile_lon = static_cast<int>(std::floor(coord->lon));
        int64_t key = tile_key(tile_lat, tile_lon);

        auto it = tile_cache.find(key);
        if (it == tile_cache.end()) {
            std::string fname = tile_filename(tile_lat, tile_lon);
            std::string path = srtm_dir + "/" + fname;
            if (std::filesystem::exists(path)) {
                tile_cache.emplace(key, load_hgt(path, tile_lat, tile_lon));
                it = tile_cache.find(key);
            } else {
                continue;  // No tile available for this location
            }
        }

        double lat_frac = coord->lat - tile_lat;
        double lon_frac = coord->lon - tile_lon;
        result.node_elevation[v] = it->second.interpolate(lat_frac, lon_frac);
    }

    return result;
}

ElevationData elevation_from_array(std::vector<double> elevations) {
    ElevationData result;
    result.node_elevation = std::move(elevations);
    return result;
}

static constexpr char ELEV_MAGIC[4] = {'G', 'E', 'L', 'V'};
static constexpr uint32_t ELEV_VERSION = 1;

void save_elevation(const ElevationData& elev, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open file for writing: " + path);

    io::write_magic(out, ELEV_MAGIC);
    io::write_u32(out, ELEV_VERSION);
    io::write_vec(out, elev.node_elevation);
}

ElevationData load_elevation(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file for reading: " + path);

    io::read_magic(in, ELEV_MAGIC);
    uint32_t version = io::read_u32(in);
    if (version != ELEV_VERSION) {
        throw std::runtime_error("Unsupported elevation version: " + std::to_string(version));
    }

    ElevationData result;
    result.node_elevation = io::read_vec<double>(in);
    return result;
}

}  // namespace gravel
