/**
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

import { Config as C4Config, ChainConfig } from './types.js';

export const default_config: {
  [key: string]: {
    alias: string[];
    beacon_apis: string[];
    rpcs: string[];
    prover: string[];
    checkpointz: string[];
    pollingInterval: number;
  }
} = {
  '1': { // mainnet
    alias: ["mainnet", "eth", "0x1"],
    beacon_apis: ["https://lodestar-mainnet.chainsafe.io"],
    rpcs: ["https://rpc.ankr.com/eth"],
    prover: ["https://mainnet1.colibri-proof.tech"],
    checkpointz: [], //"https://sync-mainnet.beaconcha.in", "https://beaconstate.info", "https://sync.invis.tools", "https://beaconstate.ethstaker.cc"],
    pollingInterval: 12000,
  },
  '11155111': { // Sepolia
    alias: ["sepolia", "0xaa36a7"],
    beacon_apis: ["https://ethereum-sepolia-beacon-api.publicnode.com"],
    rpcs: ["https://ethereum-sepolia-rpc.publicnode.com"],
    prover: ["https://sepolia.colibri-proof.tech"],
    checkpointz: [], // No public checkpointz for Sepolia yet
    pollingInterval: 12000,
  },
  '100': { // gnosis
    alias: ["gnosis", "xdai", "0x64"],
    beacon_apis: ["https://gnosis.colibri-proof.tech"],
    rpcs: ["https://rpc.ankr.com/gnosis"],
    prover: ["https://gnosis.colibri-proof.tech"],
    checkpointz: [], // TODO: Add Gnosis checkpointz servers
    pollingInterval: 5000,
  },
  '10200': { // gnosis chiado
    alias: ["chiado", "0x27d8"],
    beacon_apis: ["https://gnosis-chiado-beacon-api.publicnode.com"],
    rpcs: ["https://gnosis-chiado-rpc.publicnode.com"],
    prover: ["https://chiado.colibri-proof.tech"],
    checkpointz: [], // No public checkpointz for Chiado yet
    pollingInterval: 5000,
  },
};

export function get_chain_id(chain_id: string): number {
  const chain_id_num = parseInt(chain_id);
  if (!isNaN(chain_id_num)) return chain_id_num;
  for (const chain in default_config) {
    if (default_config[chain].alias.includes(chain_id))
      return parseInt(chain);
  }
  throw new Error("Invalid chain id: " + chain_id);
}

export function chain_conf(config: C4Config, chainId: number | string): ChainConfig | undefined {
  const k = config?.chains?.[chainId as number];
  if (k) return k;
  const k2 = default_config[chainId + ''];
  if (k2) return k2 as any;
  return undefined;
}

// Friendly aliases (optional, keep compatibility with planned naming)
export const defaultChainConfig = default_config;
export const resolveChainId = get_chain_id;
export const getChainConfig = chain_conf;


