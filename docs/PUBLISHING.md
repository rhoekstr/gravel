# Publishing Checklist

Step-by-step guide for releasing Gravel publicly on GitHub, PyPI, and conda-forge.

## Pre-release checklist

- [ ] Update `version` in `pyproject.toml`
- [ ] Update `release` and `version` in `docs/conf.py`
- [ ] Update `CHANGELOG.md` — move "Unreleased" section to a dated version heading
- [ ] Update version in `conda-recipe/meta.yaml`
- [ ] Run full test suite: `ctest --test-dir build -E "Real OSM"` (and Real OSM if data available)
- [ ] Verify `pip install -e .` works on a fresh venv
- [ ] Verify examples run: `python examples/python/01_basic_routing.py`
- [ ] Verify the GitHub username is set correctly to `rhoekstr` in:
  - [ ] `README.md`
  - [ ] `pyproject.toml` project URLs
  - [ ] `docs/installation.md`
  - [ ] `conda-recipe/meta.yaml`
- [ ] Build docs locally: `cd docs && make html`, open `_build/html/index.html`

## GitHub release

1. **Push to GitHub**
   ```bash
   git add -A
   git commit -m "Release v2.1.0"
   git push origin main
   ```

2. **Create release tag**
   ```bash
   git tag -a v2.1.0 -m "Gravel v2.1.0"
   git push origin v2.1.0
   ```

3. **Create GitHub Release** (via web UI or `gh release create`)
   - Title: `v2.1.0`
   - Description: copy the CHANGELOG entries for this version
   - The `wheels.yml` workflow triggers on tag push and publishes to PyPI

4. **Verify GitHub Actions**
   - Check `CI` workflow passes on `main`
   - Check `Build wheels + publish to PyPI` workflow starts on the tag

## PyPI release

### First-time setup (one-time)

1. **Register on PyPI**: https://pypi.org/account/register/

2. **Set up Trusted Publisher** (OIDC, no API token needed):
   - Go to https://pypi.org/manage/account/publishing/
   - Add a "pending publisher" for:
     - PyPI Project Name: `gravel-routing`
     - Owner: `rhoekstr`
     - Repository: `gravel`
     - Workflow: `wheels.yml`
     - Environment: `pypi`

3. **Create GitHub environment**:
   - Settings → Environments → New → name: `pypi`
   - (Optional) Add required reviewers

### Each release

The `wheels.yml` workflow handles everything automatically on tag push:
- Builds wheels for Linux (x86_64, aarch64), macOS (x86_64, arm64), Windows (x86_64)
- Builds source distribution
- Publishes to PyPI via Trusted Publisher

Verify at https://pypi.org/project/gravel-routing/

## conda-forge release

### First-time submission (one-time)

1. **Fork conda-forge/staged-recipes**: https://github.com/conda-forge/staged-recipes

2. **Add the recipe**:
   ```bash
   cp conda-recipe/meta.yaml ~/staged-recipes/recipes/gravel-routing/meta.yaml
   # Edit to use a real sha256 (see step 3)
   ```

3. **Get the sha256**:
   ```bash
   curl -sL https://github.com/rhoekstr/gravel/archive/refs/tags/v2.1.0.tar.gz \
       | sha256sum
   ```
   Paste into `meta.yaml` replacing `PLACEHOLDER_SHA256_OF_RELEASE_TARBALL`.

4. **Submit PR** to staged-recipes with:
   - Title: "Add gravel-routing"
   - Description: brief overview

5. **Await review**: conda-forge maintainers will review; they typically request minor changes within a week.

Once merged, a `conda-forge/gravel-routing-feedstock` repo is auto-created and you get commit access.

### Subsequent releases

After the feedstock exists, conda-forge automatically detects new PyPI/GitHub releases via the `regro-cf-autotick-bot`. You just review the auto-generated PR and merge.

## Docs deployment

### One-time setup

1. **Enable GitHub Pages**: Settings → Pages → Source: `GitHub Actions`
2. **Enable the `docs.yml` workflow**: it's already committed

### Each release

Pushes to `main` automatically trigger docs deployment. Verify at `https://rhoekstr.github.io/gravel/`.

## Post-release

- [ ] Announce on relevant forums (r/GIS, OpenStreetMap-dev, research Slack/Discord)
- [ ] Update project homepage with release notes
- [ ] File next-milestone issues for planned features
- [ ] Monitor issue tracker for bug reports

## Troubleshooting

### "PyPI upload failed: 403 Forbidden"
Trusted Publisher not configured. See "First-time setup" above.

### "wheel build failed on Windows"
Check `cibuildwheel` output; Windows builds sometimes fail due to missing DLLs. Use `CIBW_BEFORE_BUILD_WINDOWS` to install extra deps.

### "conda-forge build fails"
Check the build logs — usually a missing dependency in `meta.yaml`. Amend the PR with the fix.

### "Sphinx docs build fails"
Run `cd docs && make html` locally first. Common issues: missing autodoc mock imports, broken cross-references.
