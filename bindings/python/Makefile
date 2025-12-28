# Makefile for Colibri Python Bindings
# Copyright (c) 2025 corpus.core
# SPDX-License-Identifier: MIT

.PHONY: help build clean install install-dev test test-verbose lint format check docs examples all

# Default Python executable
PYTHON ?= python3

# Directories
SCRIPT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
PROJECT_ROOT := $(SCRIPT_DIR)/../..

help: ## Show this help message
	@echo "Colibri Python Bindings - Available commands:"
	@echo ""
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Environment variables:"
	@echo "  PYTHON     Python executable to use (default: python3)"
	@echo "  BUILD_JOBS Number of parallel build jobs"
	@echo ""

build: ## Build the Python extension module
	@echo "🔨 Building Colibri Python extension..."
	./build.sh

build-debug: ## Build in debug mode
	@echo "🔨 Building Colibri Python extension (debug)..."
	./build.sh --debug

clean: ## Clean build artifacts
	@echo "🧹 Cleaning build artifacts..."
	./build.sh --clean
	rm -rf build/ dist/ *.egg-info/
	rm -rf src/colibri/_native.* src/colibri/*.so
	find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
	find . -name "*.pyc" -delete 2>/dev/null || true

install: build ## Build and install the package
	@echo "📦 Installing Colibri Python package..."
	$(PYTHON) -m pip install .

install-dev: ## Install in development mode with dev dependencies
	@echo "📦 Installing Colibri Python package (development mode)..."
	$(PYTHON) -m pip install -e ".[dev]"

install-deps: ## Install development dependencies
	@echo "📦 Installing development dependencies..."
	$(PYTHON) -m pip install -r requirements.txt
	$(PYTHON) -m pip install pytest pytest-asyncio pytest-cov black mypy ruff

test: ## Run tests
	@echo "🧪 Running tests..."
	$(PYTHON) -m pytest tests/ -v

test-verbose: ## Run tests with verbose output
	@echo "🧪 Running tests (verbose)..."
	$(PYTHON) -m pytest tests/ -v -s

test-coverage: ## Run tests with coverage report
	@echo "🧪 Running tests with coverage..."
	$(PYTHON) -m pytest tests/ --cov=colibri --cov-report=html --cov-report=term

test-unit: ## Run only unit tests
	@echo "🧪 Running unit tests..."
	$(PYTHON) -m pytest tests/ -m "unit" -v

test-integration: ## Run only integration tests (requires native module)
	@echo "🧪 Running integration tests..."
	$(PYTHON) -m pytest tests/ -m "integration" -v

lint: ## Run linting checks
	@echo "🔍 Running linting checks..."
	$(PYTHON) -m ruff check src/ tests/ examples/
	$(PYTHON) -m mypy src/colibri/

format: ## Format code with black and ruff
	@echo "✨ Formatting code..."
	$(PYTHON) -m black src/ tests/ examples/
	$(PYTHON) -m ruff check --fix src/ tests/ examples/

check: lint test ## Run all checks (lint + test)

docs: ## Generate documentation (placeholder)
	@echo "📚 Documentation generation not yet implemented"
	@echo "See README.md for usage instructions"

examples: ## Run example scripts
	@echo "🎯 Running basic usage example..."
	$(PYTHON) examples/basic_usage.py
	@echo ""
	@echo "🎯 Running testing example..."
	$(PYTHON) examples/testing_example.py

benchmark: ## Run performance benchmarks (placeholder)
	@echo "⚡ Benchmarking not yet implemented"

verify: build test ## Build and test (useful for CI)
	@echo "✅ Build and test completed successfully"

all: clean install-dev test lint ## Clean, install, test, and lint everything

# Development workflow targets
dev-setup: ## Set up development environment
	@echo "🛠️  Setting up development environment..."
	$(PYTHON) -m pip install --upgrade pip
	$(PYTHON) -m pip install build wheel setuptools
	$(PYTHON) -m pip install pybind11
	make install-deps
	@echo "✅ Development environment ready"

dev-build: ## Quick development build (no clean)
	@echo "🔨 Quick development build..."
	./build.sh

dev-test: ## Quick test run (no verbose output)
	@echo "🧪 Quick test run..."
	$(PYTHON) -m pytest tests/ -q

dev-check: dev-build dev-test ## Quick build and test for development

# Release targets
dist: clean ## Build distribution packages
	@echo "📦 Building distribution packages..."
	$(PYTHON) -m build

release-test: dist ## Test release package
	@echo "🚀 Testing release package..."
	$(PYTHON) -m pip install --force-reinstall dist/*.whl
	$(PYTHON) -c "import colibri; print(f'Colibri {colibri.__version__} imported successfully')"

# CI targets
ci-install: ## Install for CI environment
	$(PYTHON) -m pip install --upgrade pip wheel setuptools
	$(PYTHON) -m pip install pybind11
	make install-deps

ci-build: ## Build for CI environment using integrated CMake
	cd $(PROJECT_ROOT) && cmake -B build -DC4_PYTHON=ON && make -C build -j$(BUILD_JOBS)

ci-test: ## Test for CI environment
	$(PYTHON) -m pytest tests/ --junitxml=test-results.xml --cov=colibri --cov-report=xml

# Info targets
info: ## Show build information
	@echo "📋 Build Information"
	@echo "==================="
	@echo "Python: $(shell $(PYTHON) --version)"
	@echo "Python path: $(shell which $(PYTHON))"
	@echo "Script dir: $(SCRIPT_DIR)"
	@echo "Project root: $(PROJECT_ROOT)"
	@echo "pip version: $(shell $(PYTHON) -m pip --version)"
	@echo ""
	@echo "Dependencies:"
	@$(PYTHON) -c "import pybind11; print(f'  pybind11: {pybind11.__version__}')" 2>/dev/null || echo "  pybind11: Not installed"
	@$(PYTHON) -c "import pytest; print(f'  pytest: {pytest.__version__}')" 2>/dev/null || echo "  pytest: Not installed"
	@$(PYTHON) -c "import aiohttp; print(f'  aiohttp: {aiohttp.__version__}')" 2>/dev/null || echo "  aiohttp: Not installed"