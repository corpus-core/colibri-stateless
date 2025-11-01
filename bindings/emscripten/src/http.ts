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
import { getC4w, as_char_ptr, copy_to_c } from './wasm.js';
import { Config as C4Config, DataRequest } from './types.js';

export type AcceptKind = 'json' | 'octet';

function joinPath(base: string, path?: string): string {
  if (!path) return base;
  if (!path.length) return base;
  return base + (path.startsWith('/') ? path : '/' + path);
}

/**
 * @param servers List of base URLs
 * @param path Optional path to append to server URL
 * @param method HTTP method
 * @param payload Optional JSON payload (POST)
 * @param accept Response type hint
 * @param excludeMask Bitmask to skip servers by index
 * @return Uint8Array of response bytes and nodeIndex used
 */
export async function fetch_from_servers(
  servers: string[],
  path: string,
  method: 'GET' | 'POST',
  payload?: any,
  accept: AcceptKind = 'json',
  excludeMask = 0
): Promise<{ data: Uint8Array, nodeIndex: number }> {
  let lastError = 'All nodes failed';
  let nodeIndex = 0;
  for (const server of servers) {
    if (excludeMask & (1 << nodeIndex)) {
      nodeIndex++;
      continue;
    }
    try {
      const response = await fetch(joinPath(server, path), {
        method,
        body: payload ? JSON.stringify(payload) : undefined,
        headers: {
          'Content-Type': 'application/json',
          'Accept': accept === 'json' ? 'application/json' : 'application/octet-stream',
        },
      });
      if (!response.ok) {
        lastError = `HTTP error! Status: ${response.status}, Details: ${await response.text()}`;
        nodeIndex++;
        continue;
      }
      const bytes = await response.blob().then(b => b.arrayBuffer());
      return { data: new Uint8Array(bytes), nodeIndex };
    } catch (e) {
      lastError = (e instanceof Error) ? e.message : String(e);
    }
    nodeIndex++;
  }
  throw new Error(lastError);
}

// --- Higher level helpers used by Colibri client ---


function log(msg: string) {
  console.error(msg);
}

/**
 * Posts a JSON-RPC payload across multiple URLs and returns result or proof bytes.
 * @param urls Ordered list of RPC endpoints
 * @param payload JSON-RPC payload (id will be set to 1 if not provided)
 * @param as_proof Expect octet-stream (true) or JSON result (false)
 * @return result value for JSON or raw bytes for proofs
 */
export async function fetch_rpc(urls: string[], payload: any, as_proof: boolean = false): Promise<any> {
  let last_error = 'All nodes failed';
  for (const url of urls) {
    const response = await fetch(url, {
      method: 'POST',
      body: JSON.stringify({ id: 1, jsonrpc: '2.0', ...payload }),
      headers: {
        'Content-Type': 'application/json',
        'Accept': as_proof ? 'application/octet-stream' : 'application/json',
      },
    });
    if (response.ok) {
      if (!as_proof) {
        const res = await response.json();
        if (res.error) {
          last_error = res.error?.message || res.error;
          continue;
        }
        return res.result;
      }
      const bytes = await response.blob().then(blob => blob.arrayBuffer());
      return new Uint8Array(bytes);
    } else {
      last_error = `HTTP error! Status: ${response.status}, Details: ${await response.text()}`;
    }
  }
  throw new Error(last_error);
}

/**
 * Handles a C4 data request by fetching from configured servers, setting the
 * response or error back into the C context, and leveraging cache if present.
 * @param req DataRequest descriptor from C
 * @param conf Colibri configuration
 */
export async function handle_request(req: DataRequest, conf: C4Config) {
  const free_buffers: number[] = [];
  let servers: string[] = [];
  switch (req.type) {
    case 'checkpointz':
      servers = [...(conf.checkpointz || []), ...(conf.beacon_apis || []), ...(conf.prover || [])];
      break;
    case 'beacon_api':
      servers = [...(conf.beacon_apis || []), ...(conf.prover || [])];
      break;
    case 'prover':
      servers = [...(conf.prover || [])];
      break;
    default:
      servers = conf.rpcs || [...((conf.prover || []).map(p => p + (p.endsWith('/') ? '' : '/') + 'unverified_rpc'))];
      break;
  }
  const c4w = await getC4w();
  let path = (req.type == 'eth_rpc' && req.payload)
    ? `rpc: ${req.payload?.method}(${req.payload?.params.join(',')})`
    : req.url;

  let cacheable = conf.cache && conf.cache.cacheable(req);
  if (cacheable && conf.cache) {
    const data = conf.cache.get(req);
    if (data) {
      if (conf.debug) log(`::: ${path} (len=${data.length} bytes) CACHED`);
      c4w._c4w_req_set_response(req.req_ptr, copy_to_c(data, c4w), data.length, 0);
      return;
    }
  }
  try {
    const accept = req.encoding == 'json' ? 'json' : 'octet';
    const { data, nodeIndex } = await fetch_from_servers(servers, req.url || '', req.method as any, req.payload, accept as any, req.exclude_mask);
    c4w._c4w_req_set_response(req.req_ptr, copy_to_c(data, c4w), data.length, nodeIndex);
    if (conf.debug) log(`::: ${path} (len=${data.length} bytes) FETCHED`);
    if (conf.cache && cacheable) conf.cache.set(req, data);
  } catch (e) {
    const last_error = (e instanceof Error) ? e.message : String(e);
    c4w._c4w_req_set_error(req.req_ptr, as_char_ptr(last_error, c4w, free_buffers), 0);
    if (conf.debug) log(`::: ${path} (Error: ${last_error})`);
  } finally {
    free_buffers.forEach(ptr => c4w._free(ptr));
  }
}


