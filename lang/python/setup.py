# setup.py
# ----------
# Build script for the mirage Python package.

import os
import sys
from setuptools import setup, Extension, find_packages

# Repo root
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
IS_WINDOWS = sys.platform.startswith("win")
IS_LINUX = sys.platform.startswith("linux")

# -----------------------------------------------------------------
# The compiled extension - links against pre-built mirage shared library
# -----------------------------------------------------------------
extra_link_args = ["-ldl"] if IS_LINUX else []
extra_compile_args = ["-Os"]
if not IS_WINDOWS:
    extra_compile_args.extend(["-flto", "-march=native", "-mtune=native"])

_mirage_ext = Extension(
    name="mirage._mirage",
    sources=["mirage/_mirage.c"],
    include_dirs=[os.path.join(ROOT, "include")],
    library_dirs=[os.path.join(ROOT, "build")],
    libraries=["mirage"],
    extra_link_args=extra_link_args,
    extra_compile_args=extra_compile_args,
)

# -----------------------------------------------------------------
# The package metadata
# -----------------------------------------------------------------
setup(
    name="mirage-format",
    version="1.0.0",
    description="Python bindings for the Mirage WXF serialization library",
    author="Alec Graves",
    packages=find_packages(),
    ext_modules=[_mirage_ext],
    install_requires=["numpy>=1.20"],
    zip_safe=True,
    python_requires=">=3.9",
)
