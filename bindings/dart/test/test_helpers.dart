/// Import JSON handling for mock RPC payloads and fixture parsing.
import 'dart:convert';

/// Import file system utilities for reading fixtures.
import 'dart:io';

/// Import typed byte arrays for storage values.
import 'dart:typed_data';

/// Import Colibri types used by the storage interface.
import 'package:colibri_stateless/colibri.dart';

/// Import HTTP request/response types for the mock responder.
import 'package:http/http.dart' as http;

/// Resolve the root directory containing `test/data` fixtures.
Directory resolveTestDataRoot() {
  /// Build a path relative to the current working directory.
  final root = Directory(
    [
      Directory.current.path,
      '..',
      '..',
      'test',
      'data',
    ].join(Platform.pathSeparator),
  ).absolute;
  return root;
}

/// Discover test directories that contain a `test.json` fixture.
List<Directory> discoverTestDirs() {
  /// Start from the root fixture directory.
  final root = resolveTestDataRoot();
  if (!root.existsSync()) {
    return [];
  }
  /// Only keep directories with a `test.json` config file.
  return root
      .listSync()
      .whereType<Directory>()
      .where((dir) => File('${dir.path}${Platform.pathSeparator}test.json').existsSync())
      .toList();
}

/// Storage that reads values from files within a fixture directory.
class FileBackedStorage implements ColibriStorage {
  /// Create storage bound to a single fixture directory.
  FileBackedStorage(this.testDir);

  /// Directory containing files referenced by storage keys.
  final Directory testDir;
  /// Cache loaded values to avoid repeated disk reads.
  final Map<String, Uint8List?> _cache = {};
  /// Track per-key access counts to prevent recursion loops.
  final Map<String, int> _accessCount = {};
  /// Cap repeated reads for the same key.
  final int _maxAccessPerKey = 5;

  @override
  /// Load a value for `key`, or return null when not found.
  Uint8List? get(String key) {
    /// Increment access count for recursion protection.
    _accessCount[key] = (_accessCount[key] ?? 0) + 1;
    if (_accessCount[key]! > _maxAccessPerKey) {
      /// If we exceed the cap, return cached value (possibly null).
      return _cache[key];
    }
    /// Return cached values immediately.
    if (_cache.containsKey(key)) {
      return _cache[key];
    }
    /// Attempt to find a fixture file that matches the key.
    final file = _findFileWithTruncation(key);
    if (file != null) {
      /// Read bytes from disk and cache.
      final bytes = file.readAsBytesSync();
      _cache[key] = bytes;
      return bytes;
    }
    /// Record cache miss to avoid repeated lookups.
    _cache[key] = null;
    return null;
  }

  @override
  /// Store a value in the in-memory cache only.
  void set(String key, Uint8List value) {
    /// Copy the list to avoid accidental mutations by callers.
    _cache[key] = Uint8List.fromList(value);
  }

  @override
  /// Remove a cached value for `key`.
  void delete(String key) {
    _cache.remove(key);
  }

  /// Locate a file by exact name or by truncated prefix.
  File? _findFileWithTruncation(String filename) {
    /// Try the exact filename first.
    final direct = File('${testDir.path}${Platform.pathSeparator}$filename');
    if (direct.existsSync()) {
      return direct;
    }

    /// If the name is short, do not attempt truncation heuristics.
    if (filename.length <= 200) {
      return null;
    }

    /// Split into base name and extension for prefix matching.
    final parts = filename.split('.');
    final extension = parts.length > 1 ? parts.last : null;
    final base = extension == null
        ? filename
        : filename.substring(0, filename.length - extension.length - 1);

    /// Try a range of prefix lengths to mimic truncated fixture filenames.
    final prefixes = [250, 240, 230, 220, 200, 150, 100];
    for (final prefixLen in prefixes) {
      if (base.length <= prefixLen) {
        continue;
      }
      /// Use the prefix to search for candidate fixture files.
      final prefix = base.substring(0, prefixLen);
      final candidates = testDir.listSync().whereType<File>().where((file) {
        if (extension == null) {
          return file.path.split(Platform.pathSeparator).last.startsWith(prefix);
        }
        final name = file.path.split(Platform.pathSeparator).last;
        return name.startsWith(prefix) && name.endsWith('.$extension');
      });
      if (candidates.isNotEmpty) {
        /// Use the first match to keep behavior deterministic.
        return candidates.first;
      }
    }

    return null;
  }
}

/// HTTP responder that serves fixture files based on request content.
class FileBasedMockResponder {
  /// Create a responder bound to a fixture directory.
  FileBasedMockResponder(this.testDir);

  /// Directory containing mock response files.
  final Directory testDir;
  /// Track how many requests were handled to avoid runaway loops.
  int _requestCount = 0;
  /// Hard cap on requests per test case.
  final int _maxRequests = 50;

  /// Handle an HTTP request by mapping it to a fixture file.
  Future<http.Response> handle(http.Request request) async {
    /// Increment the request count and enforce a safety limit.
    _requestCount++;
    if (_requestCount > _maxRequests) {
      return http.Response('Too many requests', 500);
    }

    /// Determine response encoding based on Accept header.
    final accept = request.headers['accept'] ?? 'application/json';
    final encoding = accept.contains('application/octet-stream') ? 'ssz' : 'json';

    /// Build the expected filename based on URL + JSON-RPC payload.
    final filename = _buildFilename(request, encoding);
    final file = _findFileWithTruncation(filename);
    if (file != null) {
      /// Serve the fixture file directly when found.
      final bytes = file.readAsBytesSync();
      return http.Response.bytes(bytes, 200);
    }

    /// Try a fallback based on method name when filename resolution fails.
    final fallback = _fallbackByMethod(request);
    if (fallback != null) {
      return http.Response.bytes(fallback.readAsBytesSync(), 200);
    }

    /// Special-case beacon fixtures with partial naming.
    if (filename.contains('light_client_updates')) {
      final fallback = _findFirstMatch('light_client_updates');
      if (fallback != null) {
        return http.Response.bytes(fallback.readAsBytesSync(), 200);
      }
    }

    if (request.url.path.contains('beacon/headers')) {
      final fallback = _findFirstMatch('headers');
      if (fallback != null) {
        return http.Response.bytes(fallback.readAsBytesSync(), 200);
      }
    }

    if (request.url.path.contains('beacon/blocks')) {
      final fallback = _findFirstMatch('blocks');
      if (fallback != null) {
        return http.Response.bytes(fallback.readAsBytesSync(), 200);
      }
    }

    /// If nothing matches, return a 500 with a helpful error.
    return http.Response('Mock file not found: $filename', 500);
  }

  /// Construct a fixture filename from URL and/or JSON-RPC payload.
  String _buildFilename(http.Request request, String encoding) {
    /// Normalize URL path by removing the leading slash.
    final path = request.url.path.startsWith('/')
        ? request.url.path.substring(1)
        : request.url.path;
    /// Preserve query string when present.
    final query = request.url.hasQuery ? '?${request.url.query}' : '';

    /// Prefer a JSON-RPC derived filename when possible.
    final rpcBaseName = _tryBuildRpcBaseName(request);

    String baseName;
    if (rpcBaseName != null) {
      baseName = rpcBaseName;
    } else if (path.isEmpty || path == '/') {
      /// Use a placeholder when neither path nor RPC data is available.
      baseName = 'unknown';
    } else {
      /// Fall back to URL-based naming for REST endpoints.
      baseName = path + query;
    }

    /// Sanitize to match fixture naming constraints.
    baseName = _sanitize(baseName);
    if (baseName.length > 100) {
      /// Truncate to keep filenames manageable.
      baseName = baseName.substring(0, 100);
    }

    /// Append the content encoding as the extension.
    return '$baseName.$encoding';
  }

  /// Try to derive a filename based on JSON-RPC method and params.
  String? _tryBuildRpcBaseName(http.Request request) {
    /// If there is no body, we cannot parse JSON-RPC data.
    if (request.body.isEmpty) {
      return null;
    }
    /// Parse the payload and ensure it is a JSON object.
    final payload = jsonDecode(request.body);
    if (payload is! Map<String, dynamic>) {
      return null;
    }
    /// Extract the RPC method name.
    final method = payload['method']?.toString();
    if (method == null || method.isEmpty) {
      return null;
    }
    /// Start the filename with the method name.
    final params = payload['params'];
    final buffer = StringBuffer(method);

    /// Special-case debug_traceCall to mirror C mock naming.
    if (method == 'debug_traceCall' && params is List && params.isNotEmpty) {
      final first = params.first;
      if (first is Map<String, dynamic>) {
        final toValue = first['to']?.toString() ?? '';
        final dataValue = first['data']?.toString() ?? '';
        buffer.write('___to___${toValue}___data___${dataValue}');
        return buffer.toString();
      }
    }

    /// Append parameters in a deterministic order.
    if (params is List) {
      for (final param in params) {
        if (param is String) {
          /// Strings are appended directly with an underscore prefix.
          buffer.write('_$param');
        } else {
          /// Non-strings are JSON encoded to match fixture generation.
          buffer.write('_${jsonEncode(param)}');
        }
      }
    }

    return buffer.toString();
  }

  /// Replace characters that are unsafe or inconsistent in filenames.
  String _sanitize(String value) {
    const forbidden = ['/', '\\', '.', ',', ' ', ':', '"', '&', '=', '[', ']', '{', '}', '?'];
    var result = value;
    for (final char in forbidden) {
      result = result.replaceAll(char, '_');
    }
    return result;
  }

  /// Find the first file containing a token in its name.
  File? _findFirstMatch(String token) {
    /// Scan the fixture directory for a name match.
    final matches = testDir
        .listSync()
        .whereType<File>()
        .where((file) => file.path.split(Platform.pathSeparator).last.contains(token))
        .toList();
    if (matches.isEmpty) {
      return null;
    }
    /// Use the first match for deterministic behavior.
    return matches.first;
  }

  /// Fallback lookup by JSON-RPC method name when filenames differ.
  File? _fallbackByMethod(http.Request request) {
    /// Only JSON-RPC requests can use method-based lookup.
    if (request.body.isEmpty) {
      return null;
    }
    try {
      /// Parse the JSON-RPC payload.
      final payload = jsonDecode(request.body);
      if (payload is! Map<String, dynamic>) {
        return null;
      }
      final method = payload['method']?.toString();
      if (method == null || method.isEmpty) {
        return null;
      }
      /// Find any file whose name starts with the method.
      final matches = testDir
          .listSync()
          .whereType<File>()
          .where((file) => file.path.split(Platform.pathSeparator).last.startsWith(method))
          .toList();
      if (matches.length == 1) {
        /// Only accept a single unambiguous match.
        return matches.first;
      }
      return null;
    } catch (_) {
      /// Ignore JSON parsing errors and disable fallback.
      return null;
    }
  }

  /// Locate a file by exact name or by truncated prefix.
  File? _findFileWithTruncation(String filename) {
    /// Try the exact filename first.
    final direct = File('${testDir.path}${Platform.pathSeparator}$filename');
    if (direct.existsSync()) {
      return direct;
    }

    /// If the name is short, do not attempt truncation heuristics.
    if (filename.length <= 200) {
      return null;
    }

    /// Split into base name and extension for prefix matching.
    final parts = filename.split('.');
    final extension = parts.length > 1 ? parts.last : null;
    final base = extension == null
        ? filename
        : filename.substring(0, filename.length - extension.length - 1);

    /// Try a range of prefix lengths to mimic truncated fixture filenames.
    final prefixes = [250, 240, 230, 220, 200, 150, 100];
    for (final prefixLen in prefixes) {
      if (base.length <= prefixLen) {
        continue;
      }
      /// Use the prefix to search for candidate fixture files.
      final prefix = base.substring(0, prefixLen);
      final candidates = testDir.listSync().whereType<File>().where((file) {
        if (extension == null) {
          return file.path.split(Platform.pathSeparator).last.startsWith(prefix);
        }
        final name = file.path.split(Platform.pathSeparator).last;
        return name.startsWith(prefix) && name.endsWith('.$extension');
      });
      if (candidates.isNotEmpty) {
        /// Use the first match to keep behavior deterministic.
        return candidates.first;
      }
    }

    return null;
  }
}
