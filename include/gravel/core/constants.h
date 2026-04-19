#pragma once

// Numeric constants used throughout the library.
//
// We use std::numbers (C++20) rather than the POSIX M_PI macro because M_PI
// is a GNU extension to <cmath> — exposed by glibc and libc++ but not by
// MSVC's C runtime unless _USE_MATH_DEFINES is defined before <cmath>.
// Routing every translation unit through <numbers> keeps the library
// portable without preprocessor gymnastics.

#include <numbers>

namespace gravel {

inline constexpr double PI = std::numbers::pi_v<double>;
inline constexpr double TWO_PI = 2.0 * std::numbers::pi_v<double>;
inline constexpr double DEG_TO_RAD = std::numbers::pi_v<double> / 180.0;
inline constexpr double RAD_TO_DEG = 180.0 / std::numbers::pi_v<double>;

}  // namespace gravel
