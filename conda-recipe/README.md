# conda-forge recipe

Working drafts of the `gravel-fragility` conda-forge recipe, staged
here before submission to
[conda-forge/staged-recipes](https://github.com/conda-forge/staged-recipes).

## Status

| File | Status |
|---|---|
| `meta.yaml` | v2.2.1 draft. `source.sha256` is a placeholder — regenerate after the v2.2.1 GitHub release tarball is live. |
| `patches/01-pybind11-spectra-find-package.patch` | Draft. Line numbers are approximate and must be regenerated against the final v2.2.1 release source before submission. |

## Why patches are needed

Upstream v2.2.1 uses `FIND_PACKAGE_ARGS` on three of five `FetchContent` dependencies (`nlohmann_json`, `Catch2`, `Eigen3`) — these find the conda-forge host packages automatically. The other two (`pybind11`, `spectra`) are left as git-clone-only because:

- **pybind11**: adding `FIND_PACKAGE_ARGS` caused scikit-build-core's isolated-env pybind11 to inject a malformed `-flto=` flag on macOS PyPI wheel builds. Dropping the arg avoided the PyPI regression.
- **Spectra**: consumed via a raw `${spectra_SOURCE_DIR}/include` path instead of an imported target, so if `find_package` takes over via `FIND_PACKAGE_ARGS`, the variable is unset and includes break.

On conda-forge both are resolvable cleanly:
- The conda-forge `pybind11` package doesn't have the scikit-build-core isolation problem.
- The conda-forge `spectra-cpp` package ships a `SpectraConfig.cmake` with an imported target.

The patch swaps both to `find_package` for conda-forge builds only. Upstream's git-clone path is untouched for PyPI / developer builds.

## Before submitting to staged-recipes

1. **Wait for v2.2.1 GitHub release** (tag + tarball published).
2. **Update `source.sha256`** in `meta.yaml`:
   ```bash
   curl -L https://github.com/rhoekstr/gravel/archive/refs/tags/v2.2.1.tar.gz | shasum -a 256
   ```
3. **Regenerate the patch** against the actual v2.2.1 release source so line numbers are exact:
   ```bash
   git clone https://github.com/rhoekstr/gravel.git && cd gravel
   git checkout v2.2.1
   # apply the conceptual change to cmake/Dependencies.cmake, then:
   git diff cmake/Dependencies.cmake > /path/to/conda-recipe/patches/01-pybind11-spectra-find-package.patch
   ```
4. **Verify locally** with `conda-build`:
   ```bash
   mamba install -c conda-forge conda-build boa
   conda mambabuild conda-recipe/
   # Should produce a gravel-fragility-2.2.1-py312_0.tar.bz2 in conda-bld/
   ```
5. **Open PR** at [conda-forge/staged-recipes](https://github.com/conda-forge/staged-recipes):
   - Fork, clone
   - `cp -r ../gravel/conda-recipe recipes/gravel-fragility`
   - Commit, push, open PR titled `Add gravel-fragility`

## Recipe conventions checklist

- [x] Apache-2.0 license_file set
- [x] recipe-maintainers includes `rhoekstr`
- [x] `host` deps use conda-forge package names (`nlohmann_json`, `spectra-cpp`)
- [x] `{{ compiler('cxx') }}` + `{{ stdlib('c') }}` for cross-compile shims
- [x] `skip: true  # [py<310]` since we require Python 3.10+
- [x] Import test + version test + functional test + OSM-presence test
- [x] `GRAVEL_USE_OSMIUM=ON` override (conda-forge has libosmium, PyPI doesn't)
- [x] build with `--no-build-isolation --no-deps` (use conda host deps)

## Once accepted

conda-forge auto-creates `conda-forge/gravel-fragility-feedstock`. Future version bumps are PRs to that feedstock's `recipe/meta.yaml` — just update `version` + `sha256` and let `regro-cf-autotick-bot` handle most of it.
