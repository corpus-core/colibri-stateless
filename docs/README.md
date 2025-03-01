# Colibri Project Metrics Dashboard

This GitHub Pages site provides visualizations and access to various metrics from the Colibri project:

## Metrics Tracked

- **Memory Usage**: Heap and stack usage tracked over time
- **Code Coverage**: Test coverage percentage tracked over time
- **Executable Size**: Size of compiled binaries tracked over time
- **Static Analysis**: Code quality issues detected by static analysis tools

## Reports

- **Coverage Reports**: Detailed HTML coverage reports showing which lines of code are covered by tests
- **Memory Analysis**: Detailed reports on memory usage patterns 
- **Static Analysis Reports**: HTML reports showing potential code issues detected by Clang's static analyzer

## Implementation Details

This dashboard is automatically updated by the GitHub Actions workflows defined in the project repository.
Every time the main workflow runs successfully, metrics are collected and the dashboard is updated.

The data visualization is implemented using Chart.js for interactive time-series charts.

## Static Analysis

The static analysis is performed using Clang's scan-build tool, which detects various issues like:
- Memory leaks
- Null pointer dereferences
- Use-after-free bugs
- Uninitialized variables
- Logic errors
- And many other potential bugs

When issues are found, they are displayed in an HTML report with detailed explanations and code snippets. 