/**
 * C wrapper implementation for evmone library
 */
#include "evmone_c_wrapper.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>
#include <evmone/evmone.h>

static_assert(EVMC_ABI_VERSION == 18, "evmone wrapper requires EVMC ABI 18 (evmone >= 0.23)");

namespace {

evmc_revision map_revision(int revision) noexcept {
  switch (revision) {
  case EVMONE_REV_OSAKA:
    return EVMC_OSAKA;
  default:
    // Unknown Colibri revision IDs fall back to Osaka until Amsterdam is activated.
    return EVMC_OSAKA;
  }
}

} // namespace

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

    evmc_bytes32 result = m_adapter.c_interface->get_storage(
        m_adapter.context,
        reinterpret_cast<const evmc_address*>(&addr),
        reinterpret_cast<const evmc_bytes32*>(&key));

    return *reinterpret_cast<const evmc::bytes32*>(&result);
  }

  evmc_storage_status set_storage(const evmc::address& addr, const evmc::bytes32& key,
                                  const evmc::bytes32& value) noexcept override {
    if (!m_adapter.c_interface->set_storage) return static_cast<evmc_storage_status>(EVMONE_STORAGE_ASSIGNED);

    evmone_storage_status status = m_adapter.c_interface->set_storage(
        m_adapter.context,
        reinterpret_cast<const evmc_address*>(&addr),
        reinterpret_cast<const evmc_bytes32*>(&key),
        reinterpret_cast<const evmc_bytes32*>(&value));

    return static_cast<evmc_storage_status>(status);
  }

  evmc::bytes32 get_balance(const evmc::address& addr) const noexcept override {
    if (!m_adapter.c_interface->get_balance) return {};

    evmc_bytes32 result = m_adapter.c_interface->get_balance(
        m_adapter.context, reinterpret_cast<const evmc_address*>(&addr));

    return *reinterpret_cast<const evmc::bytes32*>(&result);
  }

  uint64_t get_nonce(const evmc::address& addr) const noexcept override {
    if (!m_adapter.c_interface->get_nonce) return 0;
    return m_adapter.c_interface->get_nonce(m_adapter.context,
                                            reinterpret_cast<const evmc_address*>(&addr));
  }

  size_t get_code_size(const evmc::address& addr) const noexcept override {
    if (!m_adapter.c_interface->get_code_size) return 0;
    return m_adapter.c_interface->get_code_size(m_adapter.context,
                                                reinterpret_cast<const evmc_address*>(&addr));
  }

  evmc::bytes32 get_code_hash(const evmc::address& addr) const noexcept override {
    if (!m_adapter.c_interface->get_code_hash) return {};

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
    c_msg.is_static    = (msg.flags & EVMC_STATIC) != 0;
    c_msg.is_delegated = (msg.flags & EVMC_DELEGATED) != 0;
    c_msg.depth        = msg.depth;
    c_msg.gas          = msg.gas;
    c_msg.destination  = *reinterpret_cast<const evmc_address*>(&msg.recipient);
    c_msg.sender       = *reinterpret_cast<const evmc_address*>(&msg.sender);
    c_msg.input_data   = msg.input_data;
    c_msg.input_size   = msg.input_size;
    c_msg.value        = *reinterpret_cast<const evmc_bytes32*>(&msg.value);
    c_msg.code_address = *reinterpret_cast<const evmc_address*>(&msg.code_address);

    evmone_result c_result{};
    m_adapter.c_interface->call(m_adapter.context, &c_msg, msg.code, msg.code_size, &c_result);

    // Transfer ownership of host-provided output buffers without copying.
    // The Colibri host keeps results alive until evmone_release_result().
    evmc_result raw{};
    raw.status_code = static_cast<evmc_status_code>(c_result.status_code);
    raw.gas_left    = static_cast<int64_t>(c_result.gas_left);
    raw.gas_refund  = static_cast<int64_t>(c_result.gas_refund);
    raw.output_data = c_result.output_data;
    raw.output_size = c_result.output_size;
    raw.release     = nullptr;
    return evmc::Result{raw};
  }

  evmc_tx_context get_tx_context() const noexcept override {
    if (!m_adapter.c_interface->get_tx_context) return {};
    evmone_tx_context c_tx_ctx{};
    m_adapter.c_interface->get_tx_context(m_adapter.context, &c_tx_ctx);
    evmc_tx_context result{};
    result.tx_gas_price      = *reinterpret_cast<const evmc_uint256be*>(&c_tx_ctx.tx_gas_price);
    result.tx_origin         = *reinterpret_cast<const evmc_address*>(&c_tx_ctx.tx_origin);
    result.block_coinbase    = *reinterpret_cast<const evmc_address*>(&c_tx_ctx.block_coinbase);
    result.block_number      = c_tx_ctx.block_number;
    result.block_timestamp   = c_tx_ctx.block_timestamp;
    result.block_gas_limit   = c_tx_ctx.block_gas_limit;
    result.block_prev_randao = *reinterpret_cast<const evmc_uint256be*>(&c_tx_ctx.block_prev_randao);
    result.chain_id          = *reinterpret_cast<const evmc_uint256be*>(&c_tx_ctx.chain_id);
    result.block_base_fee    = *reinterpret_cast<const evmc_uint256be*>(&c_tx_ctx.block_base_fee);
    result.blob_base_fee     = *reinterpret_cast<const evmc_uint256be*>(&c_tx_ctx.blob_base_fee);
    result.blob_hashes       = c_tx_ctx.blob_hashes;
    result.blob_hashes_count = c_tx_ctx.blob_hashes_count;
    result.block_slot_number = c_tx_ctx.block_slot_number;
    return result;
  }

  evmc::bytes32 get_block_hash(int64_t number) const noexcept override {
    if (!m_adapter.c_interface->get_block_hash) return {};

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
      int status = m_adapter.c_interface->access_account(
          m_adapter.context,
          reinterpret_cast<const evmc_address*>(&addr));
      return status ? EVMC_ACCESS_WARM : EVMC_ACCESS_COLD;
    }
    return EVMC_ACCESS_COLD;
  }

  evmc_access_status access_storage(const evmc::address& addr, const evmc::bytes32& key) noexcept override {
    if (m_adapter.c_interface->access_storage) {
      int status = m_adapter.c_interface->access_storage(
          m_adapter.context,
          reinterpret_cast<const evmc_address*>(&addr),
          reinterpret_cast<const evmc_bytes32*>(&key));
      return status ? EVMC_ACCESS_WARM : EVMC_ACCESS_COLD;
    }
    return EVMC_ACCESS_COLD;
  }

  evmc::bytes32 get_transient_storage(const evmc::address& addr, const evmc::bytes32& key) const noexcept override {
    if (m_adapter.c_interface->get_transient_storage) {
      evmc_bytes32 result = m_adapter.c_interface->get_transient_storage(
          m_adapter.context,
          reinterpret_cast<const evmc_address*>(&addr),
          reinterpret_cast<const evmc_bytes32*>(&key));
      evmc::bytes32 ret;
      std::memcpy(ret.bytes, result.bytes, 32);
      return ret;
    }
    return {};
  }

  void set_transient_storage(const evmc::address& addr, const evmc::bytes32& key, const evmc::bytes32& value) noexcept override {
    if (m_adapter.c_interface->set_transient_storage) {
      m_adapter.c_interface->set_transient_storage(
          m_adapter.context,
          reinterpret_cast<const evmc_address*>(&addr),
          reinterpret_cast<const evmc_bytes32*>(&key),
          reinterpret_cast<const evmc_bytes32*>(&value));
    }
  }
};

extern "C" {
struct evmc_vm* evmc_create_evmone(void) noexcept;

#if defined(EVMONE_WASM_BUILD) && defined(__EMSCRIPTEN__)
struct evmc_vm* evmone_create(void) noexcept {
  return evmc_create_evmone();
}
#endif
}

/* Create a new EVM instance */
extern "C" void* evmone_create_executor() {
#if defined(EVMONE_WASM_BUILD) && defined(__EMSCRIPTEN__)
  auto* vm = evmone_create();
#else
  auto* vm = evmc_create_evmone();
#endif
  if (!vm) return nullptr;
  if (vm->abi_version != EVMC_ABI_VERSION) {
    evmc_destroy(vm);
    return nullptr;
  }
  return vm;
}

/* Destroy an EVM instance */
extern "C" void evmone_destroy_executor(void* executor) {
  if (executor) {
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

  HostInterfaceAdapter adapter(host_interface, host_context);
  EvmoneHostAdapter    host(adapter);

  struct evmc_host_context*         context   = reinterpret_cast<struct evmc_host_context*>(&host);
  const struct evmc_host_interface* interface = &evmc::Host::get_interface();

  evmc_message cpp_msg{};
  cpp_msg.kind         = static_cast<evmc_call_kind>(msg->kind);
  cpp_msg.flags        = 0;
  if (msg->is_static) cpp_msg.flags |= EVMC_STATIC;
  if (msg->is_delegated) cpp_msg.flags |= EVMC_DELEGATED;
  cpp_msg.depth        = msg->depth;
  cpp_msg.gas          = msg->gas;
  cpp_msg.recipient    = *reinterpret_cast<const evmc_address*>(&msg->destination);
  cpp_msg.sender       = *reinterpret_cast<const evmc_address*>(&msg->sender);
  cpp_msg.input_data   = msg->input_data;
  cpp_msg.input_size   = msg->input_size;
  cpp_msg.value        = *reinterpret_cast<const evmc_bytes32*>(&msg->value);
  cpp_msg.code_address = *reinterpret_cast<const evmc_address*>(&msg->code_address);
  cpp_msg.code         = code;
  cpp_msg.code_size    = code_size;

  evmc_result cpp_result = vm->execute(vm, interface, context,
                                       map_revision(revision),
                                       &cpp_msg, code, code_size);

  evmone_result c_result{};
  c_result.status_code      = cpp_result.status_code;
  c_result.gas_left         = static_cast<uint64_t>(cpp_result.gas_left);
  c_result.gas_refund       = static_cast<uint64_t>(cpp_result.gas_refund);
  c_result.output_data      = cpp_result.output_data;
  c_result.output_size      = cpp_result.output_size;
  c_result.release_callback = reinterpret_cast<void*>(cpp_result.release);
  c_result.release_context  = nullptr;

  return c_result;
}

/* Release result resources */
extern "C" void evmone_release_result(evmone_result* result) {
  if (result && result->release_callback) {
    auto release_fn = reinterpret_cast<evmc_release_result_fn>(result->release_callback);

    evmc_result cpp_result{};
    cpp_result.output_data = result->output_data;
    cpp_result.output_size = result->output_size;

    release_fn(&cpp_result);
  }

  result->output_data      = nullptr;
  result->output_size      = 0;
  result->release_callback = nullptr;
  result->release_context  = nullptr;
}
