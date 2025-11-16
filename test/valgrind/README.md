# Valgrind Suppressions

This directory contains suppression files for Valgrind to filter out known false positives during memory leak detection.

## Why Suppressions Are Needed

### Server Tests (`test_server*`)

Server tests use multi-threading and event-driven I/O libraries that produce false positives:

1. **libuv Event Loop**
   - Maintains internal structures that are "still reachable" but not leaked
   - Uses thread-local storage that Valgrind flags as "possibly lost"

2. **libcurl + OpenSSL**
   - Global initialization leaves reachable memory
   - SSL contexts maintain caches
   - DNS resolver (getaddrinfo) allocates thread-local memory

3. **pthread Threading**
   - Thread-local storage (TLS) appears as "possibly lost"
   - Cleanup handlers are registered but not always visible to Valgrind

## Files

### `server.supp`
Suppressions for server-related false positives:
- libuv event loop internals
- pthread thread creation/cleanup
- curl global initialization
- OpenSSL library initialization
- Dynamic library loading (dlopen/dlsym)
- DNS resolution (getaddrinfo)

## How It Works in CI

The CI workflow applies different Valgrind strictness levels:

### Regular Tests (Non-Server)
```bash
valgrind --leak-check=full \
         --errors-for-leak-kinds=definite,possible \
         --error-exitcode=1 \
         test_binary
```
**Strict mode**: Fails on both `definite` and `possible` leaks.

### Server Tests (`test_server*`)
```bash
valgrind --leak-check=full \
         --errors-for-leak-kinds=definite \
         --suppressions=test/valgrind/server.supp \
         --fair-sched=yes \
         --error-exitcode=1 \
         test_binary
```
**Balanced mode**: 
- Only fails on `definite` leaks (real bugs)
- Suppresses known library false positives
- Uses `--fair-sched=yes` for better thread scheduling

## Generating New Suppressions

If you encounter new false positives:

1. Run Valgrind with suppression generation:
```bash
valgrind --leak-check=full \
         --gen-suppressions=all \
         --log-file=valgrind.log \
         ./build/test/unittests/test_server
```

2. Open `valgrind.log` and find the suppression blocks marked with `{`
3. Copy relevant suppressions to `server.supp`
4. Give them descriptive names

## Testing Locally

Run server tests under Valgrind locally:

```bash
# Build with debug symbols
cmake -B build -DTEST=true -DCURL=true -DHTTP_SERVER=true -DCMAKE_BUILD_TYPE=Debug
cd build && make test_server

# Run with suppressions
valgrind --leak-check=full \
         --suppressions=../test/valgrind/server.supp \
         --fair-sched=yes \
         --track-origins=yes \
         --show-leak-kinds=all \
         --errors-for-leak-kinds=definite \
         ./test/unittests/test_server
```

## What We're Still Catching

Even with suppressions, Valgrind will detect:

- ✅ **Use after free** in server code
- ✅ **Definite memory leaks** in request handling
- ✅ **Invalid reads/writes** in event loop callbacks
- ✅ **Uninitialized memory** in response buffers
- ✅ **Stack overflows** in recursive functions

The suppressions only filter out **known library internals**, not bugs in our code!

## References

- [Valgrind Manual: Suppression Files](https://valgrind.org/docs/manual/manual-core.html#manual-core.suppress)
- [libuv Known Issues with Valgrind](https://github.com/libuv/libuv/wiki/Debugging)
- [OpenSSL Memory Leaks (Not Really)](https://wiki.openssl.org/index.php/Libcrypto_API#Memory_Leaks)


