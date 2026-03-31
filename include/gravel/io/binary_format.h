#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace gravel::io {

// Write a POD vector to a binary stream
template <typename T>
void write_vec(std::ofstream& out, const std::vector<T>& v) {
    uint64_t n = v.size();
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));
    if (!v.empty()) {
        out.write(reinterpret_cast<const char*>(v.data()), n * sizeof(T));
    }
}

// Read a POD vector from a binary stream
template <typename T>
std::vector<T> read_vec(std::ifstream& in) {
    uint64_t n;
    in.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (!in) throw std::runtime_error("Failed to read vector size");
    std::vector<T> v(n);
    if (n > 0) {
        in.read(reinterpret_cast<char*>(v.data()), n * sizeof(T));
        if (!in) throw std::runtime_error("Failed to read vector data");
    }
    return v;
}

// Write a 4-byte magic identifier
inline void write_magic(std::ofstream& out, const char magic[4]) {
    out.write(magic, 4);
}

// Read and verify a 4-byte magic identifier
inline void read_magic(std::ifstream& in, const char expected[4]) {
    char buf[4];
    in.read(buf, 4);
    if (!in || std::memcmp(buf, expected, 4) != 0) {
        throw std::runtime_error(std::string("Invalid magic: expected ") + expected);
    }
}

inline void write_u32(std::ofstream& out, uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

inline uint32_t read_u32(std::ifstream& in) {
    uint32_t v;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    if (!in) throw std::runtime_error("Failed to read uint32");
    return v;
}

}  // namespace gravel::io
