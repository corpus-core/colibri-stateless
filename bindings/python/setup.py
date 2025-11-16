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
        return version_file.read_text(encoding="utf-8").strip()
    return "0.1.0"

# Read requirements
def get_requirements():
    req_file = Path(__file__).parent / "requirements.txt"
    if req_file.exists():
        return req_file.read_text(encoding="utf-8").strip().split('\n')
    return ['aiohttp>=3.8.0', 'typing-extensions>=4.0.0']

# Get the root directory (../../ from this file)
project_root = Path(__file__).parent.parent.parent.absolute()

# Check if native extension already exists (built by CMake)
native_extension_exists = False
native_extension_path = Path(__file__).parent / "src" / "colibri"
for pattern in ["_native*.so", "_native*.pyd", "_native*.dylib"]:
    if list(native_extension_path.glob(pattern)):
        native_extension_exists = True
        break

# Define a dummy extension to ensure platform-specific wheel is created
# The real extension is already built by CMake and copied to the right location
ext_modules = [
    Extension(
        "colibri._native",
        sources=[],  # No sources, already built
        include_dirs=[],
        libraries=[],
        library_dirs=[],
    ),
]

if not native_extension_exists:
    # If extension doesn't exist, run build script to create it
    print("Native extension not found, building with CMake...")
    try:
        build_script = Path(__file__).parent / "build.sh"
        if build_script.exists():
            subprocess.run([str(build_script)], check=True, cwd=Path(__file__).parent)
        else:
            raise FileNotFoundError("build.sh not found")
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"CMake build failed: {e}")
        print("Extension will be built by setuptools (may fail)")
        # Fallback to building with setuptools
        ext_modules = [
            Pybind11Extension(
                "colibri._native",
                [
                    "src/bindings.cpp",
                ],
                include_dirs=[
                    str(project_root / "bindings"),
                    str(project_root / "src/util"),
                    str(project_root / "src/prover"),
                    str(project_root / "src/verifier"),
                ],
                language='c++',
                cxx_std=17,
            ),
        ]

class CustomBuildExt(build_ext):
    """Custom build extension that uses pre-built extensions"""
    
    def build_extensions(self):
        # Check if extension already exists
        native_extension_path = Path(__file__).parent / "src" / "colibri"
        existing_extension = None
        
        for pattern in ["_native*.so", "_native*.pyd", "_native*.dylib"]:
            matches = list(native_extension_path.glob(pattern))
            if matches:
                existing_extension = matches[0]
                break
        
        if existing_extension and existing_extension.exists():
            print(f"Using pre-built extension: {existing_extension}")
            # For each extension module, copy the pre-built file
            for ext in self.extensions:
                if ext.name == "colibri._native":
                    # Get the target path where setuptools expects the extension
                    fullname = self.get_ext_fullname(ext.name)
                    filename = self.get_ext_filename(fullname)
                    target_path = Path(self.build_lib) / filename
                    
                    # Ensure target directory exists
                    target_path.parent.mkdir(parents=True, exist_ok=True)
                    
                    # Copy the pre-built extension
                    import shutil
                    shutil.copy2(existing_extension, target_path)
                    print(f"Copied {existing_extension} -> {target_path}")
        else:
            # No pre-built extension found, try to build normally
            print("No pre-built extension found, building normally...")
            super().build_extensions()

setup(
    name="colibri",
    version=get_version(),
    author="corpus.core",
    author_email="contact@corpus.core",
    description="Python bindings for Colibri stateless Ethereum proof library",
    long_description=open("README.md", encoding="utf-8").read(),
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