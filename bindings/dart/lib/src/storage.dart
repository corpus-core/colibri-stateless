import 'dart:typed_data';


/// Storage abstraction used by the native verifier/prover for caching.
///
/// Implement this interface and pass it to [Colibri] to provide a custom
/// cache backend (e.g. file-based or persistent).
abstract class ColibriStorage {
  /// Returns cached bytes for [key], or null if missing.
  Uint8List? get(String key);
  /// Stores [value] under [key].
  void set(String key, Uint8List value);
  /// Deletes [key] if it exists.
  void delete(String key);
}

/// Simple in-memory [ColibriStorage] implementation.
///
/// Suitable for tests or short-lived clients. Use [size] and [clear] to
/// inspect or reset the cache.
class MemoryStorage implements ColibriStorage {
  final Map<String, Uint8List> _data = {};

  @override
  Uint8List? get(String key) => _data[key];

  @override
  void set(String key, Uint8List value) {
    _data[key] = Uint8List.fromList(value);
  }

  @override
  void delete(String key) {
    _data.remove(key);
  }

  /// Number of stored entries.
  int size() => _data.length;

  /// Removes all entries.
  void clear() => _data.clear();
}

