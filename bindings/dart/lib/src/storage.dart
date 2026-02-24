import 'dart:typed_data';


/// Storage abstraction used by the native verifier/prover.
abstract class ColibriStorage {
  /// Return cached bytes for [key] or null if missing.
  Uint8List? get(String key);
  /// Store [value] under [key].
  void set(String key, Uint8List value);
  /// Delete [key] if it exists.
  void delete(String key);
}

/// Simple in-memory storage implementation.
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

  /// Remove all entries.
  void clear() => _data.clear();
}

