import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:colibri_stateless/colibri.dart';
import 'package:http/http.dart' as http;

Directory resolveTestDataRoot() {
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

List<Directory> discoverTestDirs() {
  final root = resolveTestDataRoot();
  if (!root.existsSync()) {
    return [];
  }
  return root
      .listSync()
      .whereType<Directory>()
      .where((dir) => File('${dir.path}${Platform.pathSeparator}test.json').existsSync())
      .toList();
}

class FileBackedStorage implements ColibriStorage {
  FileBackedStorage(this.testDir);

  final Directory testDir;
  final Map<String, Uint8List?> _cache = {};
  final Map<String, int> _accessCount = {};
  final int _maxAccessPerKey = 5;

  @override
  Uint8List? get(String key) {
    _accessCount[key] = (_accessCount[key] ?? 0) + 1;
    if (_accessCount[key]! > _maxAccessPerKey) {
      return _cache[key];
    }
    if (_cache.containsKey(key)) {
      return _cache[key];
    }
    final file = _findFileWithTruncation(key);
    if (file != null) {
      final bytes = file.readAsBytesSync();
      _cache[key] = bytes;
      return bytes;
    }
    _cache[key] = null;
    return null;
  }

  @override
  void set(String key, Uint8List value) {
    _cache[key] = Uint8List.fromList(value);
  }

  @override
  void delete(String key) {
    _cache.remove(key);
  }

  File? _findFileWithTruncation(String filename) {
    final direct = File('${testDir.path}${Platform.pathSeparator}$filename');
    if (direct.existsSync()) {
      return direct;
    }

    if (filename.length <= 200) {
      return null;
    }

    final parts = filename.split('.');
    final extension = parts.length > 1 ? parts.last : null;
    final base = extension == null
        ? filename
        : filename.substring(0, filename.length - extension.length - 1);

    final prefixes = [250, 240, 230, 220, 200, 150, 100];
    for (final prefixLen in prefixes) {
      if (base.length <= prefixLen) {
        continue;
      }
      final prefix = base.substring(0, prefixLen);
      final candidates = testDir.listSync().whereType<File>().where((file) {
        if (extension == null) {
          return file.path.split(Platform.pathSeparator).last.startsWith(prefix);
        }
        final name = file.path.split(Platform.pathSeparator).last;
        return name.startsWith(prefix) && name.endsWith('.$extension');
      });
      if (candidates.isNotEmpty) {
        return candidates.first;
      }
    }

    return null;
  }
}

class FileBasedMockResponder {
  FileBasedMockResponder(this.testDir);

  final Directory testDir;
  int _requestCount = 0;
  final int _maxRequests = 50;

  Future<http.Response> handle(http.Request request) async {
    _requestCount++;
    if (_requestCount > _maxRequests) {
      return http.Response('Too many requests', 500);
    }

    final accept = request.headers['accept'] ?? 'application/json';
    final encoding = accept.contains('application/octet-stream') ? 'ssz' : 'json';

    final filename = _buildFilename(request, encoding);
    final file = _findFileWithTruncation(filename);
    if (file != null) {
      final bytes = file.readAsBytesSync();
      return http.Response.bytes(bytes, 200);
    }

    final fallback = _fallbackByMethod(request);
    if (fallback != null) {
      return http.Response.bytes(fallback.readAsBytesSync(), 200);
    }

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

    return http.Response('Mock file not found: $filename', 500);
  }

  String _buildFilename(http.Request request, String encoding) {
    final path = request.url.path.startsWith('/')
        ? request.url.path.substring(1)
        : request.url.path;
    final query = request.url.hasQuery ? '?${request.url.query}' : '';

    final rpcBaseName = _tryBuildRpcBaseName(request);

    String baseName;
    if (rpcBaseName != null) {
      baseName = rpcBaseName;
    } else if (path.isEmpty || path == '/') {
      baseName = 'unknown';
    } else {
      baseName = path + query;
    }

    baseName = _sanitize(baseName);
    if (baseName.length > 100) {
      baseName = baseName.substring(0, 100);
    }

    return '$baseName.$encoding';
  }

  String? _tryBuildRpcBaseName(http.Request request) {
    if (request.body.isEmpty) {
      return null;
    }
    final payload = jsonDecode(request.body);
    if (payload is! Map<String, dynamic>) {
      return null;
    }
    final method = payload['method']?.toString();
    if (method == null || method.isEmpty) {
      return null;
    }
    final params = payload['params'];
    final buffer = StringBuffer(method);

    if (method == 'debug_traceCall' && params is List && params.isNotEmpty) {
      final first = params.first;
      if (first is Map<String, dynamic>) {
        final toValue = first['to']?.toString() ?? '';
        final dataValue = first['data']?.toString() ?? '';
        buffer.write('___to___${toValue}___data___${dataValue}');
        return buffer.toString();
      }
    }

    if (params is List) {
      for (final param in params) {
        if (param is String) {
          buffer.write('_$param');
        } else {
          buffer.write('_${jsonEncode(param)}');
        }
      }
    }

    return buffer.toString();
  }

  String _sanitize(String value) {
    const forbidden = ['/', '\\', '.', ',', ' ', ':', '"', '&', '=', '[', ']', '{', '}', '?'];
    var result = value;
    for (final char in forbidden) {
      result = result.replaceAll(char, '_');
    }
    return result;
  }

  File? _findFirstMatch(String token) {
    final matches = testDir
        .listSync()
        .whereType<File>()
        .where((file) => file.path.split(Platform.pathSeparator).last.contains(token))
        .toList();
    if (matches.isEmpty) {
      return null;
    }
    return matches.first;
  }

  File? _fallbackByMethod(http.Request request) {
    if (request.body.isEmpty) {
      return null;
    }
    try {
      final payload = jsonDecode(request.body);
      if (payload is! Map<String, dynamic>) {
        return null;
      }
      final method = payload['method']?.toString();
      if (method == null || method.isEmpty) {
        return null;
      }
      final matches = testDir
          .listSync()
          .whereType<File>()
          .where((file) => file.path.split(Platform.pathSeparator).last.startsWith(method))
          .toList();
      if (matches.length == 1) {
        return matches.first;
      }
      return null;
    } catch (_) {
      return null;
    }
  }

  File? _findFileWithTruncation(String filename) {
    final direct = File('${testDir.path}${Platform.pathSeparator}$filename');
    if (direct.existsSync()) {
      return direct;
    }

    if (filename.length <= 200) {
      return null;
    }

    final parts = filename.split('.');
    final extension = parts.length > 1 ? parts.last : null;
    final base = extension == null
        ? filename
        : filename.substring(0, filename.length - extension.length - 1);

    final prefixes = [250, 240, 230, 220, 200, 150, 100];
    for (final prefixLen in prefixes) {
      if (base.length <= prefixLen) {
        continue;
      }
      final prefix = base.substring(0, prefixLen);
      final candidates = testDir.listSync().whereType<File>().where((file) {
        if (extension == null) {
          return file.path.split(Platform.pathSeparator).last.startsWith(prefix);
        }
        final name = file.path.split(Platform.pathSeparator).last;
        return name.startsWith(prefix) && name.endsWith('.$extension');
      });
      if (candidates.isNotEmpty) {
        return candidates.first;
      }
    }

    return null;
  }
}
