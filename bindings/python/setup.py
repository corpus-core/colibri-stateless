"""
Setup script for Colibri Python bindings
"""
import os
import sys
import subprocess
from pathlib import Path

from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup, Extension, find_packages

# Read version from file if it exists
def get_version():
    version_file = Path(__file__).parent / "VERSION"
    if version_file.exists():
        return version_file.read_text().strip()
    return "0.1.0"

# Read requirements
def get_requirements():
    req_file = Path(__file__).parent / "requirements.txt"
    if req_file.exists():
        return req_file.read_text().strip().split('\n')
    return ['aiohttp>=3.8.0', 'typing-extensions>=4.0.0']

# Get the root directory (../../ from this file)
project_root = Path(__file__).parent.parent.parent.absolute()

# Define the extension module
ext_modules = [
    Pybind11Extension(
        "colibri._native",
        [
            "src/bindings.cpp",
        ],
        include_dirs=[
            str(project_root / "bindings"),
            str(project_root / "src"),
            str(project_root / "src/util"),
            str(project_root / "src/proofer"),
            str(project_root / "src/verifier"),
        ],
        libraries=["colibri"],
        library_dirs=[
            str(project_root / "build/default/src"),
            str(project_root / "build/default/libs"),
        ],
        language='c++',
        cxx_std=17,
    ),
]

class CustomBuildExt(build_ext):
    """Custom build extension that builds the C library first"""
    
    def build_extensions(self):
        # Build the C library first using our build script
        build_script = Path(__file__).parent / "build.sh"
        if build_script.exists():
            print("Building C library...")
            subprocess.check_call([str(build_script)], cwd=str(build_script.parent))
        
        super().build_extensions()

setup(
    name="colibri",
    version=get_version(),
    author="corpus.core",
    author_email="contact@corpus.core",
    description="Python bindings for Colibri stateless Ethereum proof library",
    long_description=open("README.md").read(),
    long_description_content_type="text/markdown",
    url="https://github.com/corpus-core/colibri-stateless",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    ext_modules=ext_modules,
    cmdclass={"build_ext": CustomBuildExt},
    zip_safe=False,
    python_requires=">=3.8",
    install_requires=get_requirements(),
    extras_require={
        "dev": [
            "pytest>=7.0.0",
            "pytest-asyncio>=0.21.0",
            "pytest-cov>=4.0.0",
            "black>=22.0.0",
            "mypy>=1.0.0",
            "ruff>=0.1.0",
        ]
    },
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Topic :: System :: Networking",
        "Topic :: Security :: Cryptography",
    ],
    keywords="ethereum blockchain proof verification web3 rpc",
    project_urls={
        "Bug Reports": "https://github.com/corpus-core/colibri-stateless/issues",
        "Source": "https://github.com/corpus-core/colibri-stateless",
        "Documentation": "https://github.com/corpus-core/colibri-stateless/tree/main/bindings/python",
    },
)