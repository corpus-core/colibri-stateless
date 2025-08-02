/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pybind11/chrono.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstring>
#include <functional>
#include <memory>
#include <string>

extern "C" {
#include "colibri.h"
#include "plugin.h"
}

// Undefine the bytes macro to avoid conflicts with pybind11::bytes
#ifdef bytes
#undef bytes
#endif

namespace py = pybind11;

// Helper function to create bytes_t without using the macro
static bytes_t make_bytes_t(uint8_t* data, uint32_t len) {
  bytes_t result;
  result.data = data;
  result.len  = len;
  return result;
}

// Global storage callbacks
static std::function<py::bytes(const std::string&)>       python_storage_get;
static std::function<void(const std::string&, py::bytes)> python_storage_set;
static std::function<void(const std::string&)>            python_storage_delete;

// C callback functions for storage
extern "C" {
static bool storage_get_callback(char* key, buffer_t* buffer) {
  if (!python_storage_get || !key) return false;

  try {
    py::bytes result = python_storage_get(std::string(key));
    if (result.ptr() == nullptr) return false;

    char*      data = nullptr;
    Py_ssize_t size;
    if (PyBytes_AsStringAndSize(result.ptr(), &data, &size) != 0) {
      return false;
    }

    // Allocate buffer and copy data
    buffer_grow(buffer, size + 1);
    memcpy(buffer->data.data, data, size);
    buffer->data.len = size;
    return true;
  } catch (...) {
    return false;
  }
}

static void storage_set_callback(char* key, bytes_t value) {
  if (!python_storage_set || !key) return;

  try {
    py::bytes data = py::bytes(reinterpret_cast<const char*>(value.data), value.len);
    python_storage_set(std::string(key), data);
  } catch (...) {
    // Ignore exceptions in callback
  }
}

static void storage_delete_callback(char* key) {
  if (!python_storage_delete || !key) return;

  try {
    python_storage_delete(std::string(key));
  } catch (...) {
    // Ignore exceptions in callback
  }
}
}

// Helper function to convert bytes_t to Python bytes
py::bytes bytes_t_to_python(bytes_t data) {
  return py::bytes(reinterpret_cast<const char*>(data.data), data.len);
}

// Helper function to convert Python bytes to bytes_t
bytes_t python_to_bytes_t(py::bytes data) {
  char*      buffer = nullptr;
  Py_ssize_t size;
  PyBytes_AsStringAndSize(data.ptr(), &buffer, &size);

  bytes_t result;
  result.data = reinterpret_cast<uint8_t*>(buffer);
  result.len  = static_cast<uint32_t>(size);
  return result;
}

// Wrapper functions with proper memory management
std::string proofer_execute_json_status_wrapper(proofer_t* ctx) {
  char* result = c4_proofer_execute_json_status(ctx);
  if (!result) return "";

  std::string str(result);
  free(result);
  return str;
}

std::string verify_execute_json_status_wrapper(void* ctx) {
  char* result = c4_verify_execute_json_status(ctx);
  if (!result) return "";

  std::string str(result);
  free(result);
  return str;
}

py::bytes proofer_get_proof_wrapper(proofer_t* ctx) {
  bytes_t proof = c4_proofer_get_proof(ctx);
  return bytes_t_to_python(proof);
}

void req_set_response_wrapper(uintptr_t req_ptr, py::bytes data, uint16_t node_index) {
  bytes_t bytes_data = python_to_bytes_t(data);
  c4_req_set_response(reinterpret_cast<void*>(req_ptr), bytes_data, node_index);
}

void req_set_error_wrapper(uintptr_t req_ptr, const std::string& error, uint16_t node_index) {
  c4_req_set_error(reinterpret_cast<void*>(req_ptr), const_cast<char*>(error.c_str()), node_index);
}

void* verify_create_ctx_wrapper(py::bytes proof, const std::string& method, const std::string& args, uint64_t chain_id, const std::string& trusted_block_hashes) {
  bytes_t proof_data = python_to_bytes_t(proof);
  return c4_verify_create_ctx(
      proof_data,
      const_cast<char*>(method.c_str()),
      const_cast<char*>(args.c_str()),
      chain_id,
      const_cast<char*>(trusted_block_hashes.c_str()));
}

proofer_t* create_proofer_ctx_wrapper(const std::string& method, const std::string& params, uint64_t chain_id, uint32_t flags) {
  return c4_create_proofer_ctx(
      const_cast<char*>(method.c_str()),
      const_cast<char*>(params.c_str()),
      chain_id,
      flags);
}

int get_method_support_wrapper(uint64_t chain_id, const std::string& method) {
  return c4_get_method_support(chain_id, const_cast<char*>(method.c_str()));
}

// Storage registration function
void register_storage(
    std::function<py::bytes(const std::string&)>       get_func,
    std::function<void(const std::string&, py::bytes)> set_func,
    std::function<void(const std::string&)>            delete_func) {
  python_storage_get    = get_func;
  python_storage_set    = set_func;
  python_storage_delete = delete_func;

  // Configure the C storage plugin
  storage_plugin_t plugin = {
      .get             = storage_get_callback,
      .set             = storage_set_callback,
      .del             = storage_delete_callback,
      .max_sync_states = 3};
  c4_set_storage_config(&plugin);
}

PYBIND11_MODULE(_native, m) {
  m.doc() = "Colibri native bindings for Python";

  // Storage registration
  m.def("register_storage", &register_storage,
        "Register Python storage callbacks with the C library",
        py::arg("get_func"), py::arg("set_func"), py::arg("delete_func"));

  // Proofer functions
  m.def("create_proofer_ctx", &create_proofer_ctx_wrapper,
        "Create a new proofer context",
        py::arg("method"), py::arg("params"), py::arg("chain_id"), py::arg("flags"),
        py::return_value_policy::take_ownership);

  m.def("proofer_execute_json_status", &proofer_execute_json_status_wrapper,
        "Execute the proofer and return JSON status",
        py::arg("ctx"));

  m.def("proofer_get_proof", &proofer_get_proof_wrapper,
        "Get the proof from the proofer context",
        py::arg("ctx"));

  m.def("free_proofer_ctx", &c4_free_proofer_ctx,
        "Free the proofer context",
        py::arg("ctx"));

  // Verifier functions
  m.def("create_verify_ctx", &verify_create_ctx_wrapper,
        "Create a new verification context",
        py::arg("proof"), py::arg("method"), py::arg("args"), py::arg("chain_id"), py::arg("trusted_block_hashes"),
        py::return_value_policy::take_ownership);

  m.def("verify_execute_json_status", &verify_execute_json_status_wrapper,
        "Execute verification and return JSON status",
        py::arg("ctx"));

  m.def("verify_free_ctx", &c4_verify_free_ctx,
        "Free the verification context",
        py::arg("ctx"));

  // Request handling functions
  m.def("req_set_response", &req_set_response_wrapper,
        "Set response data for a request",
        py::arg("req_ptr"), py::arg("data"), py::arg("node_index"));

  m.def("req_set_error", &req_set_error_wrapper,
        "Set error for a request",
        py::arg("req_ptr"), py::arg("error"), py::arg("node_index"));

  // Utility functions
  m.def("get_method_support", &get_method_support_wrapper,
        "Check method support type",
        py::arg("chain_id"), py::arg("method"));
}