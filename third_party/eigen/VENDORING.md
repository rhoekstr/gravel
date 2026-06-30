# Vendored Eigen

**Version:** 3.4.0
**Source:** https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
**License:** MPL2 (primary) — see the `COPYING.*` files in this directory.

Only the `Eigen/` header tree is vendored (Eigen is header-only; the project uses
`Eigen/Dense`, `Eigen/Sparse`, `Eigen/SparseCholesky`, and — via Spectra — `Eigen/Core`,
`Eigen/Eigenvalues`, `Eigen/SparseCore`). The `unsupported/` modules, tests, docs, and build
system are intentionally omitted.

## Why vendored

The build previously obtained Eigen via `FetchContent` cloning `gitlab.com/libeigen/eigen`
at build time, with `find_package(Eigen3 3.4)` as a fallback. Two problems made that
fragile for releases:

1. **gitlab.com outages** repeatedly broke the git-clone (and thus every wheel) during load
   incidents.
2. **Eigen 5.0** now ships in Homebrew and vcpkg, so `find_package(Eigen3 3.4)` no longer
   matches a system Eigen (wrong major; the project pins 3.4 for Spectra 1.0.1 compatibility).

Vendoring pins 3.4.0 exactly and makes the build fully offline / network-independent on every
platform.

## Updating

Replace the `Eigen/` directory with the headers from a new Eigen release tarball and update
the version above. `cmake/Dependencies.cmake` consumes it as the `Eigen3::Eigen` target.
