/**
 * Colibri Web Bridge – exposes colibriWebBridge.createClient() for Dart/Flutter web.
 *
 * Prerequisite: The Colibri WASM client must be loaded first and attached to window.Colibri
 * (e.g. by loading a bundle built from bindings/emscripten). Then load this script.
 *
 * Usage from Dart: getProperty(window, 'colibriWebBridge') then callMethod(bridge, 'createClient', [config]).
 */
(function () {
  'use strict';

  function createClient(config) {
    if (typeof window.Colibri === 'undefined') {
      throw new Error(
        'Colibri (WASM) not loaded. Load the Colibri WASM bundle (e.g. from colibri_flutter assets) before colibri_web_bridge.js.'
      );
    }
    var Colibri = window.Colibri;
    var cfg = {
      chainId: config.chainId,
      rpcs: config.ethRpcs || [],
      beacon_apis: config.beaconApis || [],
      prover: config.provers || [],
      checkpointz: config.checkpointz || [],
      trusted_checkpoint: config.trustedCheckpoint,
      include_code: config.includeCode,
      use_accesslist: config.useAccesslist,
      zk_proof: config.zkProof,
      checkpoint_witness_keys: config.checkpointWitnessKeys,
      privacy_mode: config.verifyFlags === 2 ? 'basic' : 'none',
      chains: {}
    };
    if (config.wasmBaseUrl) {
      if (typeof Colibri.set_wasm_url === 'function') {
        Colibri.set_wasm_url(config.wasmBaseUrl.replace(/\/?$/, '/') + 'c4w.wasm');
      }
    }
    var client = new Colibri(cfg);

    return {
      rpc: function (method, params) {
        return client.rpc(method, params || [])
          .then(function (result) { return { result: result }; })
          .catch(function (e) { return { error: e && (e.message || e.toString()) }; });
      },
      createProof: function (method, params) {
        return client.createProof(method, params || [])
          .then(function (proof) {
            var bytes = new Uint8Array(proof);
            var binary = '';
            for (var i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
            return { proofBase64: btoa(binary) };
          })
          .catch(function (e) { return { error: e && (e.message || e.toString()) }; });
      },
      verifyProof: function (method, params, proofBase64) {
        var binary = atob(proofBase64);
        var bytes = new Uint8Array(binary.length);
        for (var i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
        return client.verifyProof(method, params || [], bytes)
          .then(function (result) { return { result: result }; })
          .catch(function (e) { return { error: e && (e.message || e.toString()) }; });
      },
      close: function () {}
    };
  }

  window.colibriWebBridge = { createClient: createClient };
})();
