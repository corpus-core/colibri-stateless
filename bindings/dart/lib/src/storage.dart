import 'dart:typed_data';


abstract class ColibriStorage {
  Uint8List? get(String key);
  void set(String key, Uint8List value);
  void delete(String key);
}

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

  int size() => _data.length;

  void clear() => _data.clear();
}

