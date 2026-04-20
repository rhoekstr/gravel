#pragma once

// Cross-platform scratch-file path helper for tests.
//
// Tests historically hard-coded "/tmp/..." paths. On Windows there is no
// /tmp directory, so any fstream::open() or fopen() call with a POSIX
// temp path fails with "Cannot open file for writing." Use
// test_temp_path("foo.bin") instead to get a per-platform temp file
// (on POSIX: /tmp/foo.bin; on Windows: %TEMP%\foo.bin via
// std::filesystem::temp_directory_path()).

#include <filesystem>
#include <string>

namespace gravel::test {

inline std::string test_temp_path(const std::string& filename) {
    return (std::filesystem::temp_directory_path() / filename).string();
}

}  // namespace gravel::test
