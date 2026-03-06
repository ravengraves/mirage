# setup.py
# ----------
# Build script for the mirage Python package.

import os
from setuptools import setup, Extension, find_packages

# Repo root
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# -----------------------------------------------------------------
# The compiled extension - links against pre-built libmirage.so
# -----------------------------------------------------------------
_mirage_ext = Extension(
    name="mirage._mirage",
    sources=["mirage/_mirage.c"],
    include_dirs=[os.path.join(ROOT, "include")],
    library_dirs=[os.path.join(ROOT, "build")],
    libraries=["mirage"],
    extra_link_args=["-ldl"],
    extra_compile_args=["-Os", "-flto", "-march=native", "-mtune=native"],
)

# -----------------------------------------------------------------
# The package metadata
# -----------------------------------------------------------------
setup(
    name="mirage",
    version="1.0.0",
    description="Python bindings for the Mirage WXF serialization library",
    author="Alec Graves",
    packages=find_packages(),
    ext_modules=[_mirage_ext],
    install_requires=["numpy>=1.20"],
    zip_safe=True,
    python_requires=">=3.9",
)
