#include "gravel/ch/ch_serialization.h"
#include "gravel/io/binary_format.h"
#include "gravel/io/mapped_file.h"
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>

namespace gravel {

static constexpr char CH_MAGIC[4] = {'G', 'R', 'C', 'H'};
static constexpr uint32_t CH_VERSION = 4;

void save_ch(const ContractionResult& ch, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open file for writing: " + path);

    io::write_magic(out, CH_MAGIC);
    io::write_u32(out, CH_VERSION);
    io::write_u32(out, ch.num_nodes);

    io::write_vec(out, ch.up_offsets);
    io::write_vec(out, ch.up_targets);
    io::write_vec(out, ch.up_weights);
    io::write_vec(out, ch.up_shortcut_mid);

    io::write_vec(out, ch.down_offsets);
    io::write_vec(out, ch.down_targets);
    io::write_vec(out, ch.down_weights);
    io::write_vec(out, ch.down_shortcut_mid);

    io::write_vec(out, ch.node_levels);
    io::write_vec(out, ch.order);

    // Serialize unpack_map as parallel arrays of (key, value)
    uint32_t map_size = static_cast<uint32_t>(ch.unpack_map.size());
    io::write_u32(out, map_size);
    for (const auto& [key, val] : ch.unpack_map) {
        out.write(reinterpret_cast<const char*>(&key), sizeof(key));
        out.write(reinterpret_cast<const char*>(&val), sizeof(val));
    }
}

ContractionResult load_ch(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file for reading: " + path);

    io::read_magic(in, CH_MAGIC);
    uint32_t version = io::read_u32(in);
    if (version < 1 || version > CH_VERSION) {
        throw std::runtime_error("Unsupported CH version: " + std::to_string(version));
    }

    ContractionResult ch;
    ch.num_nodes = io::read_u32(in);

    ch.up_offsets = io::read_vec<uint32_t>(in);

    if (version <= 2) {
        // Legacy: interleaved CompactEdge
        auto up_edges = io::read_vec<CompactEdge>(in);
        ch.up_targets.reserve(up_edges.size());
        ch.up_weights.reserve(up_edges.size());
        for (const auto& e : up_edges) {
            ch.up_targets.push_back(e.target);
            ch.up_weights.push_back(e.weight);
        }
    } else {
        ch.up_targets = io::read_vec<NodeID>(in);
        ch.up_weights = io::read_vec<Weight>(in);
    }
    ch.up_shortcut_mid = io::read_vec<NodeID>(in);

    ch.down_offsets = io::read_vec<uint32_t>(in);

    if (version <= 2) {
        auto down_edges = io::read_vec<CompactEdge>(in);
        ch.down_targets.reserve(down_edges.size());
        ch.down_weights.reserve(down_edges.size());
        for (const auto& e : down_edges) {
            ch.down_targets.push_back(e.target);
            ch.down_weights.push_back(e.weight);
        }
    } else {
        ch.down_targets = io::read_vec<NodeID>(in);
        ch.down_weights = io::read_vec<Weight>(in);
    }
    ch.down_shortcut_mid = io::read_vec<NodeID>(in);

    if (version <= 3) {
        // Legacy: Level was uint16_t — read and widen
        auto levels16 = io::read_vec<uint16_t>(in);
        ch.node_levels.resize(levels16.size());
        for (size_t i = 0; i < levels16.size(); ++i) {
            ch.node_levels[i] = static_cast<Level>(levels16[i]);
        }
    } else {
        ch.node_levels = io::read_vec<Level>(in);
    }
    ch.order = io::read_vec<NodeID>(in);

    if (version >= 2) {
        uint32_t map_size = io::read_u32(in);
        ch.unpack_map.reserve(map_size);
        for (uint32_t i = 0; i < map_size; ++i) {
            uint64_t key;
            NodeID val;
            in.read(reinterpret_cast<char*>(&key), sizeof(key));
            in.read(reinterpret_cast<char*>(&val), sizeof(val));
            ch.unpack_map[key] = val;
        }
    }

    return ch;
}

namespace {
struct MmapReader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;

    template <typename T>
    T read_val() {
        if (pos + sizeof(T) > size) throw std::runtime_error("mmap CH: unexpected EOF");
        T val;
        std::memcpy(&val, data + pos, sizeof(T));
        pos += sizeof(T);
        return val;
    }

    void read_bytes(void* dst, size_t n) {
        if (pos + n > size) throw std::runtime_error("mmap CH: unexpected EOF");
        std::memcpy(dst, data + pos, n);
        pos += n;
    }

    template <typename T>
    std::vector<T> read_vec() {
        uint64_t n = read_val<uint64_t>();
        std::vector<T> v(n);
        if (n > 0) read_bytes(v.data(), n * sizeof(T));
        return v;
    }

    void check_magic(const char expected[4]) {
        char buf[4];
        read_bytes(buf, 4);
        if (std::memcmp(buf, expected, 4) != 0)
            throw std::runtime_error("mmap CH: invalid magic");
    }
};
}  // anonymous namespace

ContractionResult load_ch_mmap(const std::string& path) {
    MappedFile mf = MappedFile::open(path);
    MmapReader r{mf.data(), mf.size()};

    r.check_magic(CH_MAGIC);
    uint32_t version = r.read_val<uint32_t>();
    if (version < 1 || version > CH_VERSION) {
        throw std::runtime_error("Unsupported CH version: " + std::to_string(version));
    }

    ContractionResult ch;
    ch.num_nodes = r.read_val<uint32_t>();
    ch.up_offsets = r.read_vec<uint32_t>();

    if (version <= 2) {
        auto up_edges = r.read_vec<CompactEdge>();
        ch.up_targets.reserve(up_edges.size());
        ch.up_weights.reserve(up_edges.size());
        for (const auto& e : up_edges) {
            ch.up_targets.push_back(e.target);
            ch.up_weights.push_back(e.weight);
        }
    } else {
        ch.up_targets = r.read_vec<NodeID>();
        ch.up_weights = r.read_vec<Weight>();
    }
    ch.up_shortcut_mid = r.read_vec<NodeID>();

    ch.down_offsets = r.read_vec<uint32_t>();
    if (version <= 2) {
        auto down_edges = r.read_vec<CompactEdge>();
        ch.down_targets.reserve(down_edges.size());
        ch.down_weights.reserve(down_edges.size());
        for (const auto& e : down_edges) {
            ch.down_targets.push_back(e.target);
            ch.down_weights.push_back(e.weight);
        }
    } else {
        ch.down_targets = r.read_vec<NodeID>();
        ch.down_weights = r.read_vec<Weight>();
    }
    ch.down_shortcut_mid = r.read_vec<NodeID>();

    if (version <= 3) {
        auto levels16 = r.read_vec<uint16_t>();
        ch.node_levels.resize(levels16.size());
        for (size_t i = 0; i < levels16.size(); ++i) {
            ch.node_levels[i] = static_cast<Level>(levels16[i]);
        }
    } else {
        ch.node_levels = r.read_vec<Level>();
    }
    ch.order = r.read_vec<NodeID>();

    if (version >= 2) {
        uint32_t map_size = r.read_val<uint32_t>();
        ch.unpack_map.reserve(map_size);
        for (uint32_t i = 0; i < map_size; ++i) {
            uint64_t key = r.read_val<uint64_t>();
            NodeID val = r.read_val<NodeID>();
            ch.unpack_map[key] = val;
        }
    }

    return ch;
}

}  // namespace gravel
