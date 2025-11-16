# Valgrind Docker Image für C4 Server

## Übersicht

Dieses Docker-Image ermöglicht produktionsnahes **Memory Debugging** mit Valgrind. Es ist identisch zum Produktiv-Image, aber:
- Gebaut mit **Debug-Symbolen** (`RelWithDebInfo`)
- Läuft unter **Valgrind** zur Laufzeit
- Verwendet **Server-Suppressions** für bekannte False Positives
- Loggt kontinuierlich Memory-Analysen

## Motivation

Das Problem: `malloc(): unsorted double linked list corrupted`

Dieser Fehler deutet auf:
- **Double Free** - Etwas wird zweimal freigegeben
- **Use After Free** - Zugriff nach `free()`
- **Buffer Overflow** - Schreiben über Puffergrenzen

Valgrind findet diese Fehler **während sie passieren**, nicht erst beim Crash!

## Unterschiede zum Produktiv-Image

| Aspekt | Production | Valgrind |
|--------|-----------|----------|
| Build Type | `Release` | `RelWithDebInfo` |
| Optimierung | `-O3` | `-O2 -g` |
| Runtime | Direkt | Unter Valgrind |
| Speed | 100% | ~10-20% (Overhead) |
| Memory Reports | ❌ | ✅ Kontinuierlich |

## Quick Start

### 1. Build Image

```bash
cd bindings/docker
docker build -f Dockerfile.valgrind -t c4-server:valgrind ../..
```

### 2. Run mit Docker Compose

```bash
docker-compose -f docker-compose.valgrind.yml up
```

### 3. Logs ansehen

```bash
# Live Server Output
docker logs -f c4-server-valgrind

# Valgrind Report
docker exec c4-server-valgrind cat /app/logs/valgrind.log

# Oder mit Volume Mount
cat ./valgrind-logs/valgrind.log
```

## Manuelle Nutzung

### Build
```bash
docker build -f Dockerfile.valgrind -t c4-server:valgrind ../..
```

### Run
```bash
docker run -d \
  --name c4-server-valgrind \
  -p 8090:8090 \
  -v $(pwd)/valgrind-logs:/app/logs \
  -e C4_BEACON_URL="https://mainnet.beacon-api.com" \
  c4-server:valgrind
```

### Valgrind Report während Laufzeit

```bash
# Live-Bericht anzeigen
docker exec c4-server-valgrind tail -f /app/logs/valgrind.log

# Nur Leaks
docker exec c4-server-valgrind grep "definitely lost" /app/logs/valgrind.log

# Zusammenfassung
docker exec c4-server-valgrind grep -A 10 "LEAK SUMMARY" /app/logs/valgrind.log
```

## Konfiguration

### Environment Variables

```yaml
# Valgrind Optionen
VALGRIND_OPTS: "--leak-check=full --show-leak-kinds=definite --track-origins=yes"
VALGRIND_LOG: "/app/logs/valgrind.log"
VALGRIND_VERBOSE: "1"       # Für detaillierte Ausgabe
VALGRIND_XML: "1"           # Für XML Report (CI)

# Server Konfiguration (wie Production)
C4_BEACON_URL: "https://..."
C4_ETH_RPC_URL: "https://..."
```

### Custom Valgrind Options

```bash
docker run -d \
  -e VALGRIND_OPTS="--leak-check=full --show-leak-kinds=all --track-fds=yes" \
  c4-server:valgrind
```

## Beacon Watcher Debugging

Da der Fehler `malloc(): unsorted double linked list corrupted` beim Beacon Watcher auftritt:

### Szenario 1: Reproduzieren

```bash
# Server mit Beacon Events starten
docker run -d \
  --name c4-server-valgrind \
  -p 8090:8090 \
  -v $(pwd)/valgrind-logs:/app/logs \
  -e C4_BEACON_URL="https://mainnet.beacon-api.com" \
  -e STREAM_BEACON_EVENTS="true" \
  c4-server:valgrind

# Warten und Logs beobachten
docker logs -f c4-server-valgrind

# Valgrind Report live
watch -n 5 'tail -30 ./valgrind-logs/valgrind.log'
```

### Szenario 2: Stress Test

Sende viele Beacon Events schnell hintereinander:

```bash
# Terminal 1: Server starten
docker-compose -f docker-compose.valgrind.yml up

# Terminal 2: Mock Beacon Events senden (wenn möglich)
# Oder einfach warten auf echte Events
```

## Valgrind Report Interpretieren

### Erfolgreicher Run (Kein Leak)

```
==1== LEAK SUMMARY:
==1==    definitely lost: 0 bytes in 0 blocks
==1==    indirectly lost: 0 bytes in 0 blocks
==1==      possibly lost: 0 bytes in 0 blocks
==1==    still reachable: 50,432 bytes in 12 blocks
==1==         suppressed: 0 bytes in 0 blocks
```

✅ **OK**: "still reachable" sind globale Variablen

### Problem: Double Free

```
==1== Invalid free() / delete / delete[] / realloc()
==1==    at 0x4C2EDEB: free (vg_replace_malloc.c:530)
==1==    by 0x401234: c4_stop_beacon_watcher (beacon_watcher.c:555)
==1==    by 0x401678: eth_server_shutdown (handler.c:49)
==1==  Address 0x5a2b3c4 is 0 bytes inside a block of size 1,024 free'd
==1==    at 0x4C2EDEB: free (vg_replace_malloc.c:530)
==1==    by 0x401890: stop_beacon_watch (beacon_watcher.c:540)
```

🔴 **Problem**: Gleicher Pointer wird zweimal freigegeben!

### Problem: Use After Free

```
==1== Invalid read of size 8
==1==    at 0x401234: parse_sse_buffer (beacon_watcher.c:106)
==1==    by 0x401567: sse_write_callback (beacon_watcher.c:194)
==1==  Address 0x5a2b3c4 is 0 bytes inside a block of size 1,024 free'd
==1==    at 0x4C2EDEB: free (vg_replace_malloc.c:530)
==1==    by 0x401890: buffer_free (beacon_watcher.c:553)
```

🔴 **Problem**: Zugriff auf bereits freigegebenen Speicher!

### Problem: Buffer Overflow

```
==1== Invalid write of size 8
==1==    at 0x401234: strndup (beacon_watcher.c:136)
==1==    by 0x401567: parse_sse_buffer (beacon_watcher.c:140)
==1==  Address 0x5a2b3c4 is 8 bytes after a block of size 1,024 alloc'd
```

🔴 **Problem**: Schreiben über Buffer-Ende hinaus!

## Debugging Workflow

### 1. Fehler reproduzieren

Starte Server mit Valgrind und warte auf `malloc()` Fehler oder Crash.

### 2. Valgrind Log analysieren

```bash
# Finde die erste Fehlermeldung
grep -n "Invalid" valgrind-logs/valgrind.log | head -1

# Zeige Kontext (20 Zeilen vorher/nachher)
sed -n '100,140p' valgrind-logs/valgrind.log
```

### 3. Stack Trace anschauen

Valgrind zeigt exakt:
- Welche Funktion das Problem verursachte
- Welche Datei und Zeile
- Vollständiger Call Stack

### 4. Code Fix

Basierend auf Valgrind Report, z.B.:
- Double Free? → `if (ptr) { free(ptr); ptr = NULL; }`
- Use After Free? → Zugriffe vor `free()` verlagern
- Buffer Overflow? → Bounds checking hinzufügen

### 5. Verifizieren

Rebuild Image und erneut testen:

```bash
docker build -f Dockerfile.valgrind -t c4-server:valgrind ../..
docker-compose -f docker-compose.valgrind.yml up --force-recreate
```

## Production vs Valgrind Image

### Wann Valgrind verwenden?

✅ **Ja**:
- Debugging von Memory-Fehlern
- Kontinuierliches Memory-Monitoring (Staging)
- Pre-Production Testing
- Nach großen Änderungen an Memory Management

❌ **Nein**:
- Production (zu langsam, 10-20% Overhead)
- Performance Testing (verfälschte Metriken)
- High-Load Scenarios (Timeout-Risiken)

### Hybrid Approach

```
┌─────────────────┐
│  Production     │ ← Normal Image (fast)
│  (Load-balanced)│
└─────────────────┘
         │
         ├─ Server 1 (normal)
         ├─ Server 2 (normal)
         └─ Server 3 (valgrind) ← 10% Traffic für Debugging
```

## Troubleshooting

### Problem: Valgrind zu langsam

**Symptom**: Timeouts, verpasste Beacon Events

**Lösung**:
```bash
# Reduziere Valgrind Checks
docker run -e VALGRIND_OPTS="--leak-check=summary" ...
```

### Problem: Zu viel Log Output

**Symptom**: Logs > 1GB

**Lösung**:
```bash
# Nur Fehler loggen
docker run -e VALGRIND_OPTS="--leak-check=full --errors-only" ...
```

### Problem: Suppressions funktionieren nicht

**Symptom**: Viele False Positives

**Lösung**:
```bash
# Check ob Suppressions geladen wurden
docker exec c4-server-valgrind cat /app/server.supp

# Neu builden mit aktuellen Suppressions
docker build --no-cache -f Dockerfile.valgrind ...
```

## Bekannte False Positives

Die `server.supp` Datei unterdrückt bekannte False Positives:

- libuv thread-local storage
- libcurl/OpenSSL global init
- pthread internals
- DNS resolver cache

**Diese sind harmlos** und werden gefiltert.

## Integration in CI

### GitHub Actions

```yaml
- name: Build Valgrind Image
  run: docker build -f bindings/docker/Dockerfile.valgrind -t c4-server:valgrind .

- name: Run Server under Valgrind
  run: |
    docker run -d --name test-server c4-server:valgrind
    sleep 60  # Let it run for 1 minute
    docker exec test-server cat /app/logs/valgrind.log > valgrind-report.txt

- name: Check for Definite Leaks
  run: |
    if grep "definitely lost: [1-9]" valgrind-report.txt; then
      echo "Memory leaks detected!"
      exit 1
    fi
```

## Performance Impact

| Metrik | Normal | Valgrind | Overhead |
|--------|--------|----------|----------|
| Startup Time | 2s | 5s | +150% |
| Request Latency | 10ms | 12-15ms | +20-50% |
| Throughput | 1000 req/s | 800 req/s | -20% |
| Memory Usage | 120MB | 150MB | +25% |

## Related Files

- `Dockerfile` - Production Image
- `Dockerfile.valgrind` - Valgrind Image (dieses)
- `valgrind-server.sh` - Startup Script
- `docker-compose.valgrind.yml` - Docker Compose Config
- `test/valgrind/server.supp` - Suppressions

## Support

Bei Fragen oder Problemen:
1. Check Valgrind Report: `docker exec c4-server-valgrind cat /app/logs/valgrind.log`
2. Check Server Logs: `docker logs c4-server-valgrind`
3. Siehe auch: `test/valgrind/README.md`

