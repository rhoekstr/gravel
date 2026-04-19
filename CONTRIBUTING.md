# Contributing to Gravel

Thanks for your interest in contributing! This document describes the process for submitting changes.

## Ways to Contribute

- **Bug reports** — Open an issue with a minimal reproducer
- **Feature requests** — Open an issue describing the use case and proposed API
- **Documentation** — Fix typos, improve explanations, add examples
- **Code** — Bug fixes, new features, performance improvements

## Development Setup

### Prerequisites

- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- CMake 3.20+
- Python 3.10+ (for bindings)
- libosmium (optional, for OSM support): `brew install libosmium` / `apt install libosmium2-dev`

### Build from source

```bash
git clone https://github.com/rhoekstr/gravel.git
cd gravel

# Debug build with tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DGRAVEL_BUILD_TESTS=ON -DGRAVEL_BUILD_PYTHON=ON
cmake --build build -j

# Run tests
ctest --test-dir build --output-on-failure
```

### Optional: OSM support

```bash
cmake -B build -DGRAVEL_USE_OSMIUM=ON
```

## Code Style

### C++

- **C++20** — use modern features (concepts, ranges, `std::span`)
- **Header guards** via `#pragma once`
- **Namespace** `gravel::` for all public symbols; `gravel::internal::` for implementation details
- **Structure-of-arrays** for hot data paths
- **Doxygen-style** comments on every public function
- **No raw `new`/`delete`** — use `std::unique_ptr` / `std::make_unique`
- **`const` by default** — mark non-const only when mutation is needed
- **Include order**: own header first, then project headers, then system

### Python

- **PEP 8** style (enforced by `ruff`)
- **Type hints** for public functions
- **Docstrings** for every exported function/class

### Commit Messages

Follow the [Conventional Commits](https://www.conventionalcommits.org/) format:

```
feat(location_fragility): add directional asymmetry metric
fix(ch_query): correct handling of self-loops
docs(readme): update installation instructions
perf(bridges): reduce find_bridges memory by 40%
refactor(simplify): extract degree2 into separate file
test(scenario): add regression test for flood polygon
```

## Testing

### C++ tests (Catch2)

All new features require unit tests in `tests/test_*.cpp`:

```cpp
TEST_CASE("my_feature handles edge case X", "[my_feature]") {
    // arrange
    auto graph = make_test_graph();

    // act
    auto result = my_feature(graph);

    // assert
    REQUIRE(result.some_field == expected_value);
}
```

### Python tests (pytest)

```bash
cd python
pytest tests/
```

### Regression tests

Real-world OSM data tests live in `tests/test_real_osm.cpp` and `tests/test_performance_profile.cpp`. They require `GRAVEL_USE_OSMIUM=ON`. Don't break these without a good reason.

## Pull Request Process

1. Fork the repo and create a feature branch
2. Make your changes following the code style above
3. Add/update tests
4. Ensure `ctest --test-dir build` passes with no failures
5. Update `CHANGELOG.md` under an "Unreleased" section
6. Update `REFERENCE.md` if you added/changed public API
7. Submit a PR with a clear description of the change and its motivation

## Architecture Rules

Respect the sub-library dependency DAG (see `docs/PRD.md` section "Architecture Overview"). Any include that crosses a boundary in the wrong direction is a build error.

| Library | May depend on | Must NOT depend on |
|---------|---------------|---------------------|
| `gravel-core` | stdlib, OpenMP | Everything else |
| `gravel-ch` | core | simplify, fragility, geo, us |
| `gravel-simplify` | core, ch | fragility, geo, us |
| `gravel-fragility` | core, ch, simplify | geo, us |
| `gravel-geo` | core, simplify | fragility, us |
| `gravel-us` | geo | fragility |

## Reporting Security Issues

Do not open a public issue for security vulnerabilities. Email the maintainers directly (see repository contact).

## License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0 (see `LICENSE`).
