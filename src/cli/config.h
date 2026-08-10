#include "../util/chains.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_CURL
#include "../../libs/curl/http.h"
#endif

static char* get_default_config(char* chain_name, chain_id_t* chain_id, char* config_file) {
  if (!chain_name) chain_name = "mainnet";
  if (*chain_name >= '0' && *chain_name <= '9')
    *chain_id = atoi(chain_name);
  else if (strcmp(chain_name, "mainnet") == 0)
    *chain_id = C4_CHAIN_MAINNET;
  else if (strcmp(chain_name, "sepolia") == 0)
    *chain_id = C4_CHAIN_SEPOLIA;
  else if (strcmp(chain_name, "gnosis") == 0)
    *chain_id = C4_CHAIN_GNOSIS;
  else if (strcmp(chain_name, "chiado") == 0)
    *chain_id = C4_CHAIN_GNOSIS_CHIADO;
  else if (strcmp(chain_name, "base") == 0)
    *chain_id = C4_CHAIN_BASE;
  else {
    fprintf(stderr, "Invalid chain name: %s\n", chain_name);
    exit(EXIT_FAILURE);
  }

  char* config = NULL;
  switch (*chain_id) {
    case 1: // mainnet
      config = "{\"eth_rpc\":["
               "\"https://mainnet.colibri-proof.tech/execution\","
               "\"https://eth.drpc.org\","
               "\"https://ethereum-rpc.publicnode.com\","
               "\"https://singapore.rpc.blxrbdn.com\""
               "],"
               "\"beacon_api\":["
               "\"https://mainnet.colibri-proof.tech/consensus\","
               "\"https://gateway.tenderly.co/public/mainnet\","
               "\"https://ethereum-beacon-api.publicnode.com\""
               "],"
               "\"checkpointz\":["
               "\"https://sync-mainnet.beaconcha.in\","
               "\"https://mainnet.checkpoint.sigp.io\","
               "\"https://mainnet-checkpoint-sync.attestant.io\","
               "\"https://beaconstate-mainnet.chainsafe.io\","
               "\"https://mainnet-checkpoint-sync.stakely.io\","
               "\"https://checkpointz.pietjepuk.net\","
               "\"https://beaconstate.ethstaker.cc\""
               "],"
               "\"prover\":["
               "\"https://mainnet.colibri-proof.tech\","
               "\"https://mainnet-prover.incubed.net\","
               "\"https://mainnet.colimind.com\""
               "]}";
      break;

    case 11155111: // sepolia
      config = "{\"eth_rpc\":["
               "\"https://sepolia.colibri-proof.tech/execution\","
               "\"https://sepolia.drpc.org\","
               "\"https://ethereum-sepolia-rpc.publicnode.com\","
               "\"https://sepolia.gateway.tenderly.co\""
               "],"
               "\"beacon_api\":["
               "\"https://sepolia.colibri-proof.tech/consensus\","
               "\"https://ethereum-sepolia-beacon-api.publicnode.com\""
               "],"
               "\"checkpointz\":["
               "\"https://checkpoint-sync.sepolia.ethpandaops.io\","
               "\"https://beaconstate-sepolia.chainsafe.io\""
               "],"
               "\"prover\":["
               "\"https://sepolia.colibri-proof.tech\","
               "\"https://sepolia-prover.incubed.net\","
               "\"https://sepolia.colimind.com\""
               "]}";
      break;

    case 100: // gnosis
      config = "{\"eth_rpc\":["
               "\"https://gnosis.colibri-proof.tech/execution\","
               "\"https://rpc.gnosischain.com\","
               "\"https://rpc.gnosis.gateway.fm\","
               "\"https://gnosis-rpc.publicnode.com\""
               "],"
               "\"beacon_api\":["
               "\"https://gnosis.colibri-proof.tech/consensus\","
               "\"https://rpc-gbc.gnosischain.com\","
               "\"https://gnosis-beacon-api.publicnode.com\""
               "],"
               "\"checkpointz\":[\"https://checkpoint.gnosischain.com\"],"
               "\"prover\":["
               "\"https://gnosis.colibri-proof.tech\","
               "\"https://gnosis-prover.incubed.net\","
               "\"https://gnosis.colimind.com\""
               "]}";
      break;

    case 10200: // chiado
      config = "{\"eth_rpc\":["
               "\"https://rpc.chiado.gnosis.gateway.fm\","
               "\"https://rpc.chiadochain.net\","
               "\"https://gnosis-chiado-rpc.publicnode.com\""
               "],"
               "\"beacon_api\":[\"https://rpc-gbc.chiadochain.net\"],"
               "\"checkpointz\":[\"https://checkpoint.chiadochain.net\"],"
               "\"prover\":[\"https://chiado.colibri-proof.tech\"]}";
      break;
  }

#ifdef USE_CURL
  if (config) curl_set_config(json_parse(config));

  if (config_file) {
    config = (char*) bytes_read(config_file).data;
    curl_set_config(json_parse(config));
  }
#endif
  return config;
}

static void set_config(char* target, char* urls) {
  char* config = NULL;

  // Build JSON object { "<target>": [ ...urls... ] }
  if (!urls)
    return;
  else if (strchr(urls, ',')) {
    buffer_t    buf   = {0};
    const char* start = urls;
    const char* p     = urls;
    bool        first = true;

    bprintf(&buf, "{\"%s\":[", target);
    for (;; p++) {
      if (*p == ',' || *p == '\0') {
        const char* tstart = start;
        const char* tend   = p;
        while (tstart < tend && (*tstart == ' ' || *tstart == '\t' || *tstart == '\n' || *tstart == '\r')) tstart++;
        while (tend > tstart && (tend[-1] == ' ' || tend[-1] == '\t' || tend[-1] == '\n' || tend[-1] == '\r')) tend--;
        if (tend > tstart) {
          if (!first) bprintf(&buf, ",");
          size_t len = (size_t) (tend - tstart);
          bprintf(&buf, "\"%r\"", bytes(tstart, len));
          first = false;
        }
        if (*p == '\0') break;
        start = p + 1;
      }
    }
    config = bprintf(&buf, "]}");
  }
  else {
    // Single URL
    config = bprintf(NULL, "{\"%s\":[\"%S\"]}", target, urls);
  }
#ifdef USE_CURL
  curl_set_config(json_parse(config));
#endif
  safe_free(config);
}