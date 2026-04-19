# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 2.1.x   | :white_check_mark: |
| 2.0.x   | :x:                |
| < 2.0   | :x:                |

## Reporting a Vulnerability

If you believe you've found a security issue in Gravel, please **do not** open a public issue.

Instead, email the maintainer directly at **rhoekstr@gmail.com**. We will:

1. Acknowledge your report within 72 hours
2. Provide an initial assessment within 7 days
3. Work with you on a coordinated disclosure timeline

Please include:

- A description of the issue and its impact
- Steps to reproduce
- Affected version(s)
- Any suggested mitigation

## Scope

Gravel is a library that processes graph and geospatial data. Security-sensitive areas include:

- Parsing untrusted OSM PBF files (handled by libosmium)
- Parsing untrusted GeoJSON input (handled by nlohmann/json)
- Memory safety in C++ code (bounds checking, RAII)

Issues in the Python bindings, CLI, or build system are also in scope.

## Known Limitations

- Gravel does not sanitize user-provided file paths. If your application passes user-controlled paths to `load_osm_graph()` or similar, apply your own validation.
- Large OSM files can consume significant memory. Adversarial inputs could trigger OOM — validate file sizes if loading from untrusted sources.
