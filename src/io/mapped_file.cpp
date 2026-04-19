#include "gravel/io/mapped_file.h"

#include <cstring>
#include <stdexcept>
#include <string>

// Platform-specific memory-mapping implementations.
//
// The two backends implement the same public API (MappedFile::open,
// destructor unmapping, move semantics). Keep them side-by-side in one
// translation unit so changes stay in lockstep.

#ifdef _WIN32

// Minimal Windows surface: WIN32_LEAN_AND_MEAN strips unused COM/OLE/RPC/
// cryptography declarations; NOMINMAX prevents Windows.h from defining min/max
// macros that would collide with std::min / std::max everywhere.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

// Convert a UTF-8 file path to UTF-16 for the wide WinAPI. Without this
// conversion we'd have to use CreateFileMappingA, which interprets paths
// via the local ANSI codepage and breaks on non-ASCII paths outside
// English-locale Windows installations.
std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                    static_cast<int>(utf8.size()),
                                    nullptr, 0);
    if (wlen <= 0) {
        throw std::runtime_error("MappedFile: UTF-8 path conversion failed");
    }
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                        static_cast<int>(utf8.size()),
                        wide.data(), wlen);
    return wide;
}

// Format GetLastError() into a human-readable string, mirroring the
// std::strerror(errno) idiom used in the POSIX branch.
std::string last_error_message() {
    DWORD err = GetLastError();
    if (err == 0) return "no error";
    LPSTR buf = nullptr;
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buf), 0, nullptr);
    std::string msg = (len > 0 && buf) ? std::string(buf, len) : "unknown error";
    if (buf) LocalFree(buf);
    // FormatMessage often appends CRLF — strip trailing whitespace.
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' ||
                            msg.back() == ' '))
        msg.pop_back();
    return msg + " (code=" + std::to_string(err) + ")";
}

}  // namespace

namespace gravel {

MappedFile::~MappedFile() {
    if (data_) {
        UnmapViewOfFile(data_);
    }
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        if (data_) UnmapViewOfFile(data_);
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

MappedFile MappedFile::open(const std::string& path) {
    std::wstring wpath = utf8_to_wide(path);

    // FILE_FLAG_SEQUENTIAL_SCAN hints the Windows cache manager to optimize
    // read-ahead for forward-linear access patterns — the rough equivalent
    // of POSIX madvise(MADV_SEQUENTIAL).
    HANDLE file = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("MappedFile: cannot open " + path + ": " +
                                 last_error_message());
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size)) {
        std::string err = last_error_message();
        CloseHandle(file);
        throw std::runtime_error("MappedFile: GetFileSizeEx failed: " + err);
    }

    if (file_size.QuadPart == 0) {
        CloseHandle(file);
        throw std::runtime_error("MappedFile: file is empty: " + path);
    }

    // PAGE_READONLY + FILE_MAP_READ produces a read-only mapping. Windows
    // lazy-materializes the view, matching POSIX MAP_PRIVATE | PROT_READ
    // semantics for read-only access.
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY,
                                        0, 0, nullptr);
    if (!mapping) {
        std::string err = last_error_message();
        CloseHandle(file);
        throw std::runtime_error("MappedFile: CreateFileMapping failed: " + err);
    }

    void* ptr = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0,
                              static_cast<SIZE_T>(file_size.QuadPart));

    // Both handles can be closed immediately — the view keeps the mapping
    // alive until UnmapViewOfFile(), same as the POSIX pattern of
    // close(fd) right after mmap().
    CloseHandle(mapping);
    CloseHandle(file);

    if (!ptr) {
        throw std::runtime_error("MappedFile: MapViewOfFile failed: " +
                                 last_error_message());
    }

    MappedFile mf;
    mf.data_ = static_cast<uint8_t*>(ptr);
    mf.size_ = static_cast<size_t>(file_size.QuadPart);
    return mf;
}

}  // namespace gravel

#else  // POSIX

#include <cerrno>
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

#endif  // _WIN32
