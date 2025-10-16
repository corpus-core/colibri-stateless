# Beacon Watcher Test Documentation

## Overview

Der **Beacon Watcher Test** (`test_server_beacon_watcher.c`) testet die SSE (Server-Sent Events) Infrastruktur, die für Ethereum Beacon Chain Block-Updates verwendet wird.

## Motivation

Der Beacon Watcher empfängt alle **12 Sekunden** neue Block-Events über eine persistente HTTP-Verbindung. Ein Memory Leak in diesem Bereich würde zu kontinuierlichem Speicher-Anstieg führen (~90MB/Woche).

### Memory Leak gefunden und behoben

**Problem**: `beacon_block_t` wurde bei jedem Event allokiert, aber nie freigegeben.
**Fix**: `src/chains/eth/server/head_update.c:232` - `safe_free(beacon_block)` hinzugefügt.

## Test-Architektur

### Mock SSE Server

Da SSE eine persistente HTTP-Verbindung ist (nicht Request/Response), verwenden wir einen **leichtgewichtigen Mock SSE Server**:

```
┌─────────────────────┐
│  Mock SSE Server    │  Port 28546
│  (libuv TCP)        │
└──────────┬──────────┘
           │ SSE Stream
           │ event: head
           │ data: {...}
           │
           │ event: finalized_checkpoint
           │ data: {...}
           ▼
┌─────────────────────┐
│  Beacon Watcher     │
│  (curl multi)       │
└─────────────────────┘
```

### Komponenten

1. **Mock SSE Server** (`mock_sse_server_thread`):
   - Läuft in separatem Thread
   - Verwendet eigene `uv_loop_t`
   - Sendet SSE-Events im korrekten Format
   - Simuliert Connection Drops für Reconnect-Tests

2. **Beacon Watcher** (Produktiv-Code):
   - Verbindet zu Mock Server
   - Parst SSE Events
   - Triggert Event-Handler (werden fehlschlagen, da keine Backend-Mocks)

## Tests

### Test 1: Memory Leak Detection (`test_beacon_watcher_memory_leak`)

**Ziel**: Sicherstellen, dass SSE-Buffer und Event-Parsing keine Leaks haben.

**Ablauf**:
```c
1. Start Mock SSE Server (10 events)
2. c4_test_set_beacon_watcher_url() → Mock Server URL
3. c4_watch_beacon_events() → Start Beacon Watcher
4. sleep(2) → Events empfangen und parsen
5. c4_stop_beacon_watcher() → Cleanup
6. Stop Mock SSE Server
```

**Erwartung**:
- Valgrind: `definitely lost: 0 bytes`
- Beacon API Fehler sind **OK** (keine Mocks für Sub-Requests)

**Was wird getestet**:
- ✅ SSE Buffer Management (`buffer_splice()`)
- ✅ Event String Allocation (`strndup`, `safe_free`)
- ✅ CURL Multi Handle Cleanup
- ✅ libuv Timer/Handle Cleanup

### Test 2: Reconnect Logic (`test_beacon_watcher_reconnect`)

**Ziel**: Connection Drops ohne Memory Leaks handhaben.

**Ablauf**:
```c
1. Start Mock SSE Server (3 events)
2. c4_watch_beacon_events() → Start Watcher
3. sleep(1) → Events empfangen
4. Stop Mock SSE Server → Simulate disconnect
5. sleep(2) → Watcher erkennt Disconnect
6. Start Mock SSE Server wieder (3 events)
7. sleep(6) → Automatic reconnect (RECONNECT_DELAY_MS = 5000ms)
8. c4_stop_beacon_watcher() → Cleanup
```

**Erwartung**:
- Keine Leaks beim Disconnect/Reconnect-Zyklus
- Inactivity Timer funktioniert
- Reconnect Timer funktioniert

## SSE Event Format

Mock Server sendet Events im SSE-Standard-Format:

```
event: head
data: {"slot":"12345678","block":"0x1234...cdef"}

event: finalized_checkpoint
data: {"block":"0xabcd...7890","epoch":"12345"}

```

**Wichtig**: Jedes Event endet mit `\n\n` (doppelter Newline).

## Test Helper Function

### `c4_test_set_beacon_watcher_url(const char* url)`

**Definiert in**: `src/chains/eth/server/beacon_watcher.c` (nur mit `-DTEST`)

**Zweck**: Überschreibt die Beacon Watcher URL für Tests.

```c
// Produktiv: URL wird aus Server-Liste generiert
BEACON_WATCHER_URL = "https://mainnet.beacon-api.com/eth/v1/events?topics=..."

// Test: Auf Mock Server umleiten
c4_test_set_beacon_watcher_url("http://127.0.0.1:28546/eth/v1/events?topics=...");
```

## Bauen und Ausführen

### Build
```bash
mkdir build-test && cd build-test
cmake .. -DTEST=1 -DHTTP_SERVER=1 -DPROOFER_CACHE=1
make test_server_beacon_watcher -j8
```

**Hinweis**: `-DPROOFER_CACHE=1` ist erforderlich, da Beacon Event Handler `c4_prover_cache_invalidate()` aufrufen.

### Run
```bash
./test/unittests/test_server_beacon_watcher
```

**Erwartete Ausgabe**:
```
=== Testing Beacon Watcher SSE Stream Memory Management ===
Mock SSE: Server listening on port 28546
INFO c4_watch_beacon_events:413 Initializing beacon watcher...
Waiting for SSE events (will see API request errors, this is expected)...
✅ If Valgrind shows 0 'definitely lost', SSE infrastructure is leak-free!

=== Testing Beacon Watcher Reconnect Logic ===
✅ Connection drop handled gracefully, no leaks on reconnect!

2 Tests 0 Failures 0 Ignored 
OK
```

### Valgrind
```bash
valgrind --leak-check=full \
         --errors-for-leak-kinds=definite \
         --suppressions=../../test/valgrind/server.supp \
         ./test/unittests/test_server_beacon_watcher
```

**Erwartung**:
```
definitely lost: 0 bytes in 0 blocks       ✅
still reachable: ~50KB (globals)           ✅ OK
```

## Known Limitations

### 1. Beacon API Sub-Requests werden nicht gemockt

Die Event-Handler (`c4_handle_new_head`, `c4_handle_finalized_checkpoint`) machen weitere Beacon API Requests, die fehlschlagen werden:

```
ERROR: Failed to fetch beacon block ...
```

**Das ist OK**, weil dieser Test sich auf die **SSE-Infrastruktur** konzentriert, nicht auf die vollständige Event-Pipeline.

### 2. Produktiv-Code Fehler (Pre-existing)

Wenn ohne `-DPROOFER_CACHE=1` gebaut wird:
```
error: call to undeclared function 'c4_eth_update_finality'
error: call to undeclared function 'c4_beacon_cache_update_blockdata'
```

Diese sind **pre-existing** und nicht Teil dieses Tests.

### 3. Thread Timing

Tests verwenden `sleep()` für Timing. In sehr langsamen CI-Umgebungen könnte das flaky sein. Bisher keine Probleme beobachtet.

## CI Integration

### Automatische Test-Discovery

Der Test wird automatisch von `test/unittests/CMakeLists.txt` erkannt:

```cmake
file(GLOB TEST_SOURCES test_*.c)
# → test_server_beacon_watcher.c wird gefunden
```

### Valgrind CI

Der Test wird in der Valgrind CI mit Server-Suppressions laufen (da Name mit `test_server` beginnt):

```yaml
if [[ "$TEST_NAME" == "test_server"* ]]; then
  VALGRIND_FLAGS="$VALGRIND_FLAGS --suppressions=test/valgrind/server.supp"
  VALGRIND_FLAGS="$VALGRIND_FLAGS --errors-for-leak-kinds=definite"
fi

valgrind $VALGRIND_FLAGS ./build/test/unittests/test_server_beacon_watcher
```

**Wichtig**: Der Test heißt `test_server_beacon_watcher` (nicht `test_beacon_watcher`), damit die CI ihn als Server-Test erkennt!

**Server-Suppressions** sind notwendig für:
- libuv thread-local storage
- libcurl/OpenSSL globals
- DNS resolver cache

## Debugging

### Verbose Logging

Aktiviere `CURLOPT_VERBOSE` in `beacon_watcher.c:501`:

```c
curl_easy_setopt(watcher_state.easy_handle, CURLOPT_VERBOSE, 1L);
```

### SSE Event Dump

Kommentiere Zeile in `beacon_watcher.c:114` ein:

```c
bytes_write(bytes(buf_data + processed_len, buf_len - processed_len), 
            fopen("buf_data.txt", "w"), true);
```

### Mock Server Events

Der Mock Server loggt alle gesendeten Events:

```
Mock SSE: Sending event 1/10
Mock SSE: Sending event 2/10
...
```

## Weitere Test-Ideen

1. **Stress Test**: 1000+ Events über längere Zeit
2. **Malformed Events**: Ungültige SSE-Formate
3. **Slow Network**: Simuliere langsame Verbindung
4. **Multiple Reconnects**: Mehrere Disconnect/Reconnect-Zyklen
5. **Full Pipeline**: Mit Beacon API Mocks für kompletten Flow

## Related Files

- `src/chains/eth/server/beacon_watcher.c` - Beacon Watcher Implementation
- `src/chains/eth/server/head_update.c` - Event Handler (Fixed Memory Leak)
- `src/chains/eth/server/handler.h` - Test Helper Declaration
- `test/valgrind/server.supp` - Valgrind Suppressions
- `MEMORY_LEAK_FIX.md` - Detailed Analysis of Fixed Leak

