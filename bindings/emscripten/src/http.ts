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


