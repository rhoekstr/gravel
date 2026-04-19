"""Sphinx configuration for Gravel documentation."""

import os
import sys
from pathlib import Path

# Add Python package to path so autodoc can find it
sys.path.insert(0, str(Path(__file__).parent.parent / "python"))

project = "Gravel"
copyright = "2026, Robert Hoekstra"
author = "Robert Hoekstra"
release = "2.2.0"
version = "2.2"

# ============================================================
# Extensions
# ============================================================
extensions = [
    "sphinx.ext.autodoc",
    "sphinx.ext.napoleon",        # Google/NumPy style docstrings
    "sphinx.ext.viewcode",
    "sphinx.ext.intersphinx",
    "sphinx.ext.todo",
    "sphinx_autodoc_typehints",
    "myst_parser",                # Markdown support
    "sphinx_copybutton",          # Copy button on code blocks
]

# Myst configuration
myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "tasklist",
    "linkify",
]

# ============================================================
# Source files
# ============================================================
source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}
master_doc = "index"
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

# ============================================================
# Autodoc
# ============================================================
autodoc_default_options = {
    "members": True,
    "member-order": "bysource",
    "special-members": "__init__",
    "undoc-members": True,
    "exclude-members": "__weakref__",
}
autodoc_typehints = "description"
autodoc_mock_imports = ["_gravel"]  # C++ extension may not be importable during docs build

# ============================================================
# HTML output
# ============================================================
html_theme = "sphinx_rtd_theme"
html_static_path = ["_static"]
html_title = "Gravel Documentation"

html_theme_options = {
    "collapse_navigation": False,
    "sticky_navigation": True,
    "navigation_depth": 4,
    "style_external_links": True,
}

# ============================================================
# Intersphinx
# ============================================================
intersphinx_mapping = {
    "python": ("https://docs.python.org/3", None),
    "numpy": ("https://numpy.org/doc/stable/", None),
    "geopandas": ("https://geopandas.org/en/stable/", None),
    "shapely": ("https://shapely.readthedocs.io/en/stable/", None),
}

# ============================================================
# Todo
# ============================================================
todo_include_todos = True
