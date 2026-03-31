#include "gravel/core/graph_serialization.h"
#include "gravel/io/binary_format.h"
#include "gravel/io/mapped_file.h"
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace gravel {

static constexpr char GRAPH_MAGIC[4] = {'G', 'R', 'V', 'L'};
static constexpr uint32_t GRAPH_VERSION = 2;

void save_graph(const ArrayGraph& graph, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open file for writing: " + path);

    io::write_magic(out, GRAPH_MAGIC);
    io::write_u32(out, GRAPH_VERSION);

    // Flags: bit 0 = has_coords
    uint32_t flags = graph.raw_coords().empty() ? 0 : 1;
    io::write_u32(out, flags);

    io::write_vec(out, graph.raw_offsets());
    io::write_vec(out, graph.raw_targets());
    io::write_vec(out, graph.raw_weights());
    if (flags & 1) {
        io::write_vec(out, graph.raw_coords());
    }
}

std::unique_ptr<ArrayGraph> load_graph(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file for reading: " + path);

    io::read_magic(in, GRAPH_MAGIC);
    uint32_t version = io::read_u32(in);
    if (version != GRAPH_VERSION && version != 1) {
        throw std::runtime_error("Unsupported graph version: " + std::to_string(version));
    }

    uint32_t flags = io::read_u32(in);
    auto offsets = io::read_vec<uint32_t>(in);

    std::vector<NodeID> targets;
    std::vector<Weight> weights;

    if (version == 1) {
        // Legacy: interleaved CompactEdge {NodeID target; Weight weight}
        auto edges = io::read_vec<CompactEdge>(in);
        targets.reserve(edges.size());
        weights.reserve(edges.size());
        for (const auto& e : edges) {
            targets.push_back(e.target);
            weights.push_back(e.weight);
        }
    } else {
        targets = io::read_vec<NodeID>(in);
        weights = io::read_vec<Weight>(in);
    }

    std::vector<Coord> coords;
    if (flags & 1) {
        coords = io::read_vec<Coord>(in);
    }

    return std::make_unique<ArrayGraph>(std::move(offsets), std::move(targets),
                                        std::move(weights), std::move(coords));
}

// Helper: read a POD vector from a memory-mapped region
namespace {
struct MmapReader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;

    template <typename T>
    T read_val() {
        if (pos + sizeof(T) > size) throw std::runtime_error("mmap: unexpected EOF");
        T val;
        std::memcpy(&val, data + pos, sizeof(T));
        pos += sizeof(T);
        return val;
    }

    void read_bytes(void* dst, size_t n) {
        if (pos + n > size) throw std::runtime_error("mmap: unexpected EOF");
        std::memcpy(dst, data + pos, n);
        pos += n;
    }

    template <typename T>
    std::vector<T> read_vec() {
        uint64_t n = read_val<uint64_t>();
        std::vector<T> v(n);
        if (n > 0) {
            read_bytes(v.data(), n * sizeof(T));
        }
        return v;
    }

    void check_magic(const char expected[4]) {
        char buf[4];
        read_bytes(buf, 4);
        if (std::memcmp(buf, expected, 4) != 0) {
            throw std::runtime_error("mmap: invalid magic");
        }
    }
};
}  // anonymous namespace

std::unique_ptr<ArrayGraph> load_graph_mmap(const std::string& path) {
    MappedFile mf = MappedFile::open(path);
    MmapReader r{mf.data(), mf.size()};

    r.check_magic(GRAPH_MAGIC);
    uint32_t version = r.read_val<uint32_t>();
    if (version != GRAPH_VERSION && version != 1) {
        throw std::runtime_error("Unsupported graph version: " + std::to_string(version));
    }

    uint32_t flags = r.read_val<uint32_t>();
    auto offsets = r.read_vec<uint32_t>();

    std::vector<NodeID> targets;
    std::vector<Weight> weights;

    if (version == 1) {
        auto edges = r.read_vec<CompactEdge>();
        targets.reserve(edges.size());
        weights.reserve(edges.size());
        for (const auto& e : edges) {
            targets.push_back(e.target);
            weights.push_back(e.weight);
        }
    } else {
        targets = r.read_vec<NodeID>();
        weights = r.read_vec<Weight>();
    }

    std::vector<Coord> coords;
    if (flags & 1) {
        coords = r.read_vec<Coord>();
    }

    return std::make_unique<ArrayGraph>(std::move(offsets), std::move(targets),
                                        std::move(weights), std::move(coords));
}

}  // namespace gravel
