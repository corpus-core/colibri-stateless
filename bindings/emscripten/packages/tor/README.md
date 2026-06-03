<img src="c4_logo.png" alt="C4 Logo" width="300"/>

# @corpus-core/colibri-tor

Tor network transport for [Colibri Stateless](https://github.com/corpus-core/colibri-stateless). Routes all RPC requests through [Tor](https://www.torproject.org/) for enhanced network-level privacy.

- **Browser**: Uses [Arti](https://gitlab.torproject.org/tpo/core/arti) compiled to WebAssembly via [tor-js](https://github.com/privacy-ethereum/tor-js) -- no browser extension or external software needed.
- **Node.js**: Connects to a locally running Tor SOCKS5 proxy -- zero dependencies, pure `node:net`/`node:tls` implementation.

## Installation

```sh
npm install @corpus-core/colibri-tor @corpus-core/colibri-stateless
```

## Usage

### Browser (Arti WASM)

```typescript
import Colibri from '@corpus-core/colibri-stateless';
import { createBrowserFetch } from '@corpus-core/colibri-tor/browser';

// Bootstrap starts immediately in the background (no await needed).
// The first actual request will wait for bootstrap to complete.
const client = new Colibri({
  fetch: createBrowserFetch(),
  prover: ['https://mainnet.colibri-proof.tech']
});

// This request will await Tor bootstrap if still in progress
const balance = await client.request({
  method: 'eth_getBalance',
  params: ['0x...', 'latest']
});
```

### Node.js (SOCKS5 Proxy)

Start a Tor daemon first (e.g. `tor --SocksPort 9050` or via your system's package manager), then:

```typescript
import Colibri from '@corpus-core/colibri-stateless';
import { createSocksFetch } from '@corpus-core/colibri-tor/node';

const torFetch = await createSocksFetch({ socksPort: 9050 });

const client = new Colibri({
  fetch: torFetch,
  prover: ['https://mainnet.colibri-proof.tech']
});
```

## API

### `createBrowserFetch(options?): typeof fetch`

Creates a `fetch`-compatible function that routes requests through Tor via Arti WASM. Browser only. Returns synchronously -- Tor bootstrap starts immediately in the background and the first actual request awaits completion if needed.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `gateway` | `string` | `'https://tor-js-gateway.voltrevo.com'` | WebSocket/WebRTC gateway URL for Tor relay connections |
| `onBootstrap` | `(ms: number) => void` | `undefined` | Callback when Tor bootstrap completes |
| `logLevel` | `LogLevel` | `'warn'` | Arti log level (`'trace'` \| `'debug'` \| `'info'` \| `'warn'` \| `'error'`) |

> **Note on the gateway:** In the browser, Tor relay connections are proxied
> through a WebSocket/WebRTC gateway (browsers cannot open raw TCP sockets).
> The default `https://tor-js-gateway.voltrevo.com` is a community-operated
> gateway provided by the `tor-js` author. For production deployments you are
> strongly encouraged to run your own gateway
> (see [privacy-ethereum/tor-js-gateway](https://github.com/privacy-ethereum/tor-js-gateway))
> and pass its URL via the `gateway` option.

### `createSocksFetch(options?): Promise<typeof fetch>`

Creates a `fetch`-compatible function that routes requests through a local Tor SOCKS5 proxy. Node.js only.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `socksHost` | `string` | `'127.0.0.1'` | SOCKS5 proxy hostname |
| `socksPort` | `number` | `9050` | SOCKS5 proxy port |

## tor-js dependency

The browser transport relies on the [`tor-js`](https://www.npmjs.com/package/tor-js) npm package, which ships Arti compiled to WebAssembly. It is a regular dependency of `@corpus-core/colibri-tor` and is installed automatically via `npm install` -- no build-from-source step is required.

This package imports the `tor-js/wasm-base64` entry point, which embeds the WASM binary directly in the JavaScript bundle, so no WASM is fetched from a CDN at runtime.

## License

MIT
