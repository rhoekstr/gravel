#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>

namespace gravel {

// Memory-mapped file for zero-copy loading of binary artifacts.
// POSIX mmap on macOS/Linux; CreateFileMapping/MapViewOfFile on Windows.
// Paths are UTF-8 on all platforms. Non-copyable, movable.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    // Open and mmap the entire file. Throws on failure.
    static MappedFile open(const std::string& path);

    // Raw byte access
    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

    // Typed zero-copy view at a byte offset
    template <typename T>
    std::span<const T> as_span(size_t byte_offset, size_t count) const {
        if (byte_offset + count * sizeof(T) > size_) {
            throw std::runtime_error("MappedFile::as_span out of bounds");
        }
        return {reinterpret_cast<const T*>(data_ + byte_offset), count};
    }

    // Read a single value at byte offset
    template <typename T>
    T read_at(size_t byte_offset) const {
        if (byte_offset + sizeof(T) > size_) {
            throw std::runtime_error("MappedFile::read_at out of bounds");
        }
        T val;
        std::memcpy(&val, data_ + byte_offset, sizeof(T));
        return val;
    }

    bool is_open() const { return data_ != nullptr; }

private:
    uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

}  // namespace gravel
