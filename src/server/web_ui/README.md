# Colibri Server Web Configuration UI

This directory contains the web-based configuration interface for the Colibri Server.

## Overview

The Web UI provides a user-friendly interface to view server configuration. The HTML is automatically embedded into the server binary during build time.

**📖 For build system details, see [README_BUILD.md](README_BUILD.md)**

## Features

- **View Configuration**: Display all current server settings
- **Edit Settings**: Modify configuration values through a clean web interface
- **Validation**: Client-side validation of input values
- **Responsive Design**: Works on desktop, tablet, and mobile devices

## Building with Web UI

To enable the Web UI, build the server with the `WEB_CONFIG_UI` CMake option:

```bash
mkdir -p build/with-webui
cd build/with-webui
cmake -DHTTP_SERVER=ON -DWEB_CONFIG_UI=ON ../..
make -j4 server
```

## Accessing the UI

Once the server is running with Web UI enabled, access it at:

```
http://localhost:8090/config.html
```

(Replace `8090` with your configured port)

## Security Considerations

The Web UI is **disabled by default** (`WEB_UI_ENABLED=0`). It is currently unused in typical deployments. Enabling it (`WEB_UI_ENABLED=1` / `-u`) exposes `/config`, `/config.html`, and `/api/restart` with **no authentication, no CSRF token, and no origin check**.

Do **not** set `WEB_UI_ENABLED=1` on a network-reachable bind (`HOST=0.0.0.0`) without an authenticating reverse proxy. Residual issues when the flag is on:

- Unauthenticated read/write of server configuration (RPC/beacon URLs, TLS options) and unauthenticated restart (`POST /api/restart` → `exit(0)`).
- Config-file line injection: string values are persisted as `KEY=value` lines. A newline in a value can add extra keys, including ones the UI otherwise skips (e.g. `WITNESS_KEY`).
- `CONFIG_API_KEY` is **not implemented**. Older notes that suggested exporting it have no effect on `handle_config.c`.

If you still enable the UI, keep the server on localhost and/or put a reverse proxy with authentication in front of it:

```bash
ssh -L 8090:localhost:8090 user@server
```

## Configuration Endpoints

### GET /config
Returns the current server configuration as JSON.

**Response:**
```json
{
  "port": 8090,
  "chain_id": 1,
  "loglevel": 0,
  "rpc_nodes": "https://...",
  "beacon_nodes": "https://...",
  ...
}
```

### POST /config
Updates server configuration. Gated only by `WEB_UI_ENABLED` — there is no authentication.

**Request:**
```json
{
  "port": 8091,
  "chain_id": 1,
  "loglevel": 2
}
```

**Response:**
```json
{
  "success": true,
  "message": "Configuration updated"
}
```

### POST /config/reload
Reloads configuration from file without restarting the server.

## Customization

### Styling
The UI uses inline CSS for portability. To customize the appearance, edit `config.html`.

### Adding Fields
To add new configuration fields:

1. Add the field to the HTML form in `config.html`
2. Update the backend handler in `src/server/handle_config.c`
3. Add validation logic as needed

### Embedding in Binary
To embed the HTML file in the server binary (for easier deployment):

```bash
# Convert HTML to C header
xxd -i config.html > config_html.h
```

Then include it in the server and serve from memory.

## Disabling the Web UI

To disable the Web UI after building with it:

1. **Via Config File**: Set `WEB_CONFIG_UI=0` in `server.conf`
2. **Via Environment**: `export WEB_CONFIG_UI=0`
3. **Via Command Line**: `colibri-server --no-web-ui`

## Troubleshooting

### UI Not Loading
- Ensure server is built with `-DWEB_CONFIG_UI=ON`
- Check that port is not blocked by firewall
- Verify server logs for errors

### Changes Not Saving
- Check authentication is configured correctly
- Ensure server has write permissions to config file
- Review server logs for error messages

### Configuration Not Updating
Some configuration changes require a server restart:
```bash
# Linux
sudo systemctl restart colibri-server

# macOS
sudo launchctl stop io.corpuscore.colibri-server
sudo launchctl start io.corpuscore.colibri-server

# Windows
Restart-Service ColibriServer
```

## Alternative: CLI Configuration

If you prefer command-line tools, use the built-in configuration commands:

```bash
# View current config
colibri-server --show-config

# Update specific values
colibri-server --set port=8091 --set chain_id=11155111
```

## Development

To test the UI locally without building the server:

1. Start a mock API server:
   ```bash
   python3 -m http.server 8090
   ```

2. Modify the fetch URLs in `config.html` to point to your mock server

3. Open `config.html` in a browser

## License

This Web UI is part of the Colibri Server and is licensed under the PolyForm Noncommercial License 1.0.0 for non-commercial use. Commercial use requires a separate license.

