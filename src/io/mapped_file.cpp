#include "gravel/io/mapped_file.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gravel {

MappedFile::~MappedFile() {
    if (data_) {
        munmap(data_, size_);
    }
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        if (data_) munmap(data_, size_);
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

MappedFile MappedFile::open(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("MappedFile: cannot open " + path + ": " + std::strerror(errno));
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        ::close(fd);
        throw std::runtime_error("MappedFile: fstat failed: " + std::string(std::strerror(errno)));
    }

    size_t file_size = static_cast<size_t>(st.st_size);
    if (file_size == 0) {
        ::close(fd);
        throw std::runtime_error("MappedFile: file is empty: " + path);
    }

    void* ptr = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);

    if (ptr == MAP_FAILED) {
        throw std::runtime_error("MappedFile: mmap failed: " + std::string(std::strerror(errno)));
    }

    // Advise the kernel to read ahead
#ifdef __APPLE__
    madvise(ptr, file_size, MADV_WILLNEED);
#else
    madvise(ptr, file_size, MADV_SEQUENTIAL);
#endif

    MappedFile mf;
    mf.data_ = static_cast<uint8_t*>(ptr);
    mf.size_ = file_size;
    return mf;
}

}  // namespace gravel
