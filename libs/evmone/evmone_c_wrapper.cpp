/**
 * C wrapper implementation for evmone library
 */
#include "evmone_c_wrapper.h"

// C++ headers
#include <memory>
#include <string>
#include <vector>

// evmone headers - make sure to include these after all C++ standard headers
#include <evmc/evmc.h>
#include <evmc/evmc.hpp>
#include <evmone/evmone.h>

/* C++ to C host interface adapter */
struct HostInterfaceAdapter {
  const evmone_host_interface* c_interface;
  void*                        context;

  HostInterfaceAdapter(const evmone_host_interface* interface, void* ctx)
      : c_interface(interface), context(ctx) {}
};

/* EVMC host interface implementation that calls our C callbacks */
class EvmoneHostAdapter : public evmc::Host {
  HostInterfaceAdapter m_adapter;

public:
  EvmoneHostAdapter(const HostInterfaceAdapter& adapter) : m_adapter(adapter) {}

  bool account_exists(const evmc::address& addr) const noexcept override {
    if (!m_adapter.c_interface->account_exists) return false;
    return m_adapter.c_interface->account_exists(m_adapter.context,
                                                 reinterpret_cast<const evmc_address*>(&addr));
  }

  evmc::bytes32 get_storage(const evmc::address& addr, const evmc::bytes32& key) const noexcept override {
    if (!m_adapter.c_interface->get_storage) return {};

    // Create a temporary to safely store the result
    evmc_bytes32 result = m_adapter.c_interface->get_storage(
        m_adapter.context,
        reinterpret_cast<const evmc_address*>(&addr),
        reinterpret_cast<const evmc_bytes32*>(&key));

    return *reinterpret_cast<const evmc::bytes32*>(&result);
  }

  evmc_storage_status set_storage(const evmc::address& addr, const evmc::bytes32& key,
                                  const evmc::bytes32& value) noexcept override {
    if (!m_adapter.c_interface->set_storage) return static_cast<evmc_storage_status>(EVMONE_STORAGE_UNCHANGED);

    evmone_storage_status status = m_adapter.c_interface->set_storage(
        m_adapter.context,
        reinterpret_cast<const evmc_address*>(&addr),
        reinterpret_cast<const evmc_bytes32*>(&key),
        reinterpret_cast<const evmc_bytes32*>(&value));

    return static_cast<evmc_storage_status>(status);
  }

  evmc::bytes32 get_balance(const evmc::address& addr) const noexcept override {
    if (!m_adapter.c_interface->get_balance) return {};

    // Create a temporary to safely store the result
    evmc_bytes32 result = m_adapter.c_interface->get_balance(
        m_adapter.context, reinterpret_cast<const evmc_address*>(&addr));

    return *reinterpret_cast<const evmc::bytes32*>(&result);
  }

  size_t get_code_size(const evmc::address& addr) const noexcept override {
    if (!m_adapter.c_interface->get_code_size) return 0;
    return m_adapter.c_interface->get_code_size(m_adapter.context,
                                                reinterpret_cast<const evmc_address*>(&addr));
  }

  evmc::bytes32 get_code_hash(const evmc::address& addr) const noexcept override {
    if (!m_adapter.c_interface->get_code_hash) return {};

    // Create a temporary to safely store the result
    evmc_bytes32 result = m_adapter.c_interface->get_code_hash(
        m_adapter.context, reinterpret_cast<const evmc_address*>(&addr));

    return *reinterpret_cast<const evmc::bytes32*>(&result);
  }

  size_t copy_code(const evmc::address& addr, size_t code_offset, uint8_t* buffer_data,
                   size_t buffer_size) const noexcept override {
    if (!m_adapter.c_interface->copy_code) return 0;
    return m_adapter.c_interface->copy_code(
        m_adapter.context,
        reinterpret_cast<const evmc_address*>(&addr),
        code_offset,
        buffer_data,
        buffer_size);
  }

  // Updated return type to match EVMC API
  bool selfdestruct(const evmc::address& addr, const evmc::address& beneficiary) noexcept override {
    if (m_adapter.c_interface->selfdestruct) {
      m_adapter.c_interface->selfdestruct(
          m_adapter.context,
          reinterpret_cast<const evmc_address*>(&addr),
          reinterpret_cast<const evmc_address*>(&beneficiary));
      return true;
    }
    return false;
  }

  evmc::Result call(const evmc_message& msg) noexcept override {
    if (!m_adapter.c_interface->call) return evmc::Result{EVMC_REVERT};

    evmone_message c_msg{};
    c_msg.kind         = static_cast<decltype(c_msg.kind)>(msg.kind);
    c_msg.is_static    = msg.flags & EVMC_STATIC;
    c_msg.depth        = msg.depth;
    c_msg.gas          = msg.gas;
    c_msg.destination  = *reinterpret_cast<const evmc_address*>(&msg.recipient);
    c_msg.sender       = *reinterpret_cast<const evmc_address*>(&msg.sender);
    c_msg.input_data   = msg.input_data;
    c_msg.input_size   = msg.input_size;
    c_msg.value        = *reinterpret_cast<const evmc_bytes32*>(&msg.value);
    c_msg.create_salt  = *reinterpret_cast<const evmc_bytes32*>(&msg.create2_salt);
    c_msg.code_address = reinterpret_cast<uint64_t>(&msg.code_address);

    evmone_result c_result{};
    m_adapter.c_interface->call(m_adapter.context, &c_msg, nullptr, 0, &c_result);

    evmc::Result cpp_result;
    cpp_result.status_code = static_cast<evmc_status_code>(c_result.status_code);
    cpp_result.gas_left    = c_result.gas_left;
    cpp_result.gas_refund  = c_result.gas_refund;
    cpp_result.output_data = c_result.output_data;
    cpp_result.output_size = c_result.output_size;
    if (c_result.create_address) {
      cpp_result.create_address = *reinterpret_cast<const evmc::address*>(c_result.create_address);
    }

    // Caller must handle memory management
    return cpp_result;
  }

  evmc_tx_context get_tx_context() const noexcept override {
    if (!m_adapter.c_interface->get_tx_context) return {};
    evmc_bytes32 raw_result = m_adapter.c_interface->get_tx_context(m_adapter.context);
    // In real implementation, you'd deserialize the raw_result into a proper tx_context
    // This is simplified for demonstration
    return evmc_tx_context{};
  }

  evmc::bytes32 get_block_hash(int64_t number) const noexcept override {
    if (!m_adapter.c_interface->get_block_hash) return {};

    // Create a temporary to safely store the result
    evmc_bytes32 result = m_adapter.c_interface->get_block_hash(m_adapter.context, number);

    return *reinterpret_cast<const evmc::bytes32*>(&result);
  }

  void emit_log(const evmc::address& addr, const uint8_t* data, size_t data_size,
                const evmc::bytes32 topics[], size_t topics_count) noexcept override {
    if (m_adapter.c_interface->emit_log) {
      m_adapter.c_interface->emit_log(
          m_adapter.context,
          reinterpret_cast<const evmc_address*>(&addr),
          data,
          data_size,
          reinterpret_cast<const evmc_bytes32*>(topics),
          topics_count);
    }
  }

  evmc_access_status access_account(const evmc::address& addr) noexcept override {
    if (m_adapter.c_interface->access_account) {
      m_adapter.c_interface->access_account(
          m_adapter.context,
          reinterpret_cast<const evmc_address*>(&addr));
    }
    return EVMC_ACCESS_COLD;
  }

  evmc_access_status access_storage(const evmc::address& addr, const evmc::bytes32& key) noexcept override {
    if (m_adapter.c_interface->access_storage) {
      m_adapter.c_interface->access_storage(
          m_adapter.context,
          reinterpret_cast<const evmc_address*>(&addr),
          reinterpret_cast<const evmc_bytes32*>(&key));
    }
    return EVMC_ACCESS_COLD;
  }

  // Implement the missing methods required by newer evmc API
  evmc::bytes32 get_transient_storage(const evmc::address& addr, const evmc::bytes32& key) const noexcept override {
    // We don't have this in our C interface yet, so return default
    return {};
  }

  void set_transient_storage(const evmc::address& addr, const evmc::bytes32& key, const evmc::bytes32& value) noexcept override {
    // We don't have this in our C interface yet, so do nothing
  }
};

/* Simple holder for result's output */
struct ResultDataHolder {
  std::vector<uint8_t> output_data;
};

/* Release callback for result */
void release_result_callback(const evmc_result* result) {
  // This function is called by EVMC to release resources
  // In our case, there's nothing to do as we don't allocate extra resources
  // for EVMC results
  (void) result;
}

extern "C" {
// Forward declaration for evmc_create_evmone from evmone.h
struct evmc_vm* evmc_create_evmone(void) noexcept;

// For WASM compatibility, provide evmone_create as a wrapper around evmc_create_evmone
// This helps when the symbol is expected under a different name
#if defined(EVMONE_WASM_BUILD) && defined(__EMSCRIPTEN__)
struct evmc_vm* evmone_create(void) noexcept {
  return evmc_create_evmone();
}
#endif
}

/* Create a new EVM instance */
extern "C" void* evmone_create_executor() {
#if defined(EVMONE_WASM_BUILD) && defined(__EMSCRIPTEN__)
  // For WASM builds, we'll use a simpler approach that calls evmone_create
  // directly to avoid any linking issues with evmc_create_evmone
  return evmone_create();
#else
  // Standard path for non-WASM builds
  return evmc_create_evmone();
#endif
}

/* Destroy an EVM instance */
extern "C" void evmone_destroy_executor(void* executor) {
  if (executor) {
    // Use the evmc helper function to destroy VM
    evmc_destroy(static_cast<evmc_vm*>(executor));
  }
}

/* Execute code in the EVM */
extern "C" evmone_result evmone_execute(
    void*                        executor,
    const evmone_host_interface* host_interface,
    void*                        host_context,
    int                          revision,
    const evmone_message*        msg,
    const uint8_t*               code,
    size_t                       code_size) {

  auto* vm = static_cast<struct evmc_vm*>(executor);

  // Setup host adapter
  HostInterfaceAdapter adapter(host_interface, host_context);
  EvmoneHostAdapter    host(adapter);

  // Create host context & interface for EVMC
  struct evmc_host_context*         context   = reinterpret_cast<struct evmc_host_context*>(&host);
  const struct evmc_host_interface* interface = &evmc::Host::get_interface();

  // Convert message to evmc_message
  evmc_message cpp_msg{};
  cpp_msg.kind         = static_cast<evmc_call_kind>(msg->kind);
  cpp_msg.flags        = msg->is_static ? EVMC_STATIC : 0;
  cpp_msg.depth        = msg->depth;
  cpp_msg.gas          = msg->gas;
  cpp_msg.recipient    = *reinterpret_cast<const evmc_address*>(&msg->destination);
  cpp_msg.sender       = *reinterpret_cast<const evmc_address*>(&msg->sender);
  cpp_msg.input_data   = msg->input_data;
  cpp_msg.input_size   = msg->input_size;
  cpp_msg.value        = *reinterpret_cast<const evmc_bytes32*>(&msg->value);
  cpp_msg.create2_salt = *reinterpret_cast<const evmc_bytes32*>(&msg->create_salt);

  // Execute using the VM's execute function directly
  evmc_result cpp_result = vm->execute(vm, interface, context,
                                       static_cast<evmc_revision>(revision),
                                       &cpp_msg, code, code_size);

  // Convert result
  evmone_result c_result{};
  c_result.status_code      = cpp_result.status_code;
  c_result.gas_left         = cpp_result.gas_left;
  c_result.gas_refund       = cpp_result.gas_refund;
  c_result.output_data      = cpp_result.output_data;
  c_result.output_size      = cpp_result.output_size;
  c_result.release_callback = reinterpret_cast<void*>(cpp_result.release);
  c_result.release_context  = nullptr; // We don't need additional context

  // Handle create address if present
  if (cpp_result.create_address.bytes[0] != 0) {
    c_result.create_address = reinterpret_cast<const evmc_address*>(&cpp_result.create_address);
  }
  else {
    c_result.create_address = nullptr;
  }

  return c_result;
}

/* Release result resources */
extern "C" void evmone_release_result(evmone_result* result) {
  if (result && result->release_callback) {
    auto release_fn = reinterpret_cast<evmc_release_result_fn>(result->release_callback);

    // Create a temporary evmc_result to release
    evmc_result cpp_result{};
    cpp_result.output_data = result->output_data;
    cpp_result.output_size = result->output_size;

    // Release the resources
    release_fn(&cpp_result);
  }

  // Clear the result data to prevent double-free
  result->output_data      = nullptr;
  result->output_size      = 0;
  result->release_callback = nullptr;
  result->release_context  = nullptr;
}
