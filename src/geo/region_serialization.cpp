#include "gravel/geo/region_serialization.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace gravel {

namespace {
constexpr char MAGIC[4] = {'G', 'R', 'A', 'V'};
constexpr uint32_t VERSION = 1;

void write_u32(std::ofstream& out, uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), 4);
}

void write_i32(std::ofstream& out, int32_t v) {
    out.write(reinterpret_cast<const char*>(&v), 4);
}

void write_str(std::ofstream& out, const std::string& s) {
    write_u32(out, static_cast<uint32_t>(s.size()));
    out.write(s.data(), s.size());
}

uint32_t read_u32(std::ifstream& in) {
    uint32_t v;
    in.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

int32_t read_i32(std::ifstream& in) {
    int32_t v;
    in.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

std::string read_str(std::ifstream& in) {
    uint32_t len = read_u32(in);
    std::string s(len, '\0');
    in.read(s.data(), len);
    return s;
}
}  // namespace

void save_region_assignment(const RegionAssignment& assignment,
                            const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open for writing: " + path);

    out.write(MAGIC, 4);
    write_u32(out, VERSION);

    uint32_t n_nodes = static_cast<uint32_t>(assignment.region_index.size());
    uint32_t n_regions = static_cast<uint32_t>(assignment.regions.size());

    write_u32(out, n_nodes);
    write_u32(out, n_regions);
    write_u32(out, assignment.unassigned_count);

    // Region indices
    for (uint32_t i = 0; i < n_nodes; ++i)
        write_i32(out, assignment.region_index[i]);

    // Node counts
    for (uint32_t r = 0; r < n_regions; ++r)
        write_u32(out, assignment.region_node_counts[r]);

    // Region metadata (id + label, no polygon)
    for (uint32_t r = 0; r < n_regions; ++r) {
        write_str(out, assignment.regions[r].region_id);
        write_str(out, assignment.regions[r].label);
    }
}

RegionAssignment load_region_assignment(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open for reading: " + path);

    char magic[4];
    in.read(magic, 4);
    if (std::memcmp(magic, MAGIC, 4) != 0)
        throw std::runtime_error("Invalid region assignment file (bad magic): " + path);

    uint32_t version = read_u32(in);
    if (version != VERSION)
        throw std::runtime_error("Unsupported version " + std::to_string(version));

    uint32_t n_nodes = read_u32(in);
    uint32_t n_regions = read_u32(in);

    RegionAssignment result;
    result.unassigned_count = read_u32(in);

    result.region_index.resize(n_nodes);
    for (uint32_t i = 0; i < n_nodes; ++i)
        result.region_index[i] = read_i32(in);

    result.region_node_counts.resize(n_regions);
    for (uint32_t r = 0; r < n_regions; ++r)
        result.region_node_counts[r] = read_u32(in);

    result.regions.resize(n_regions);
    for (uint32_t r = 0; r < n_regions; ++r) {
        result.regions[r].region_id = read_str(in);
        result.regions[r].label = read_str(in);
        // polygon boundaries are empty — re-load from GeoJSON if needed
    }

    return result;
}

}  // namespace gravel
