import 'dart:typed_data';

import 'package:colibri_stateless/colibri.dart';
import 'package:test/test.dart';

void main() {
  test('MemoryStorage basic operations', () {
    final storage = MemoryStorage();
    expect(storage.size(), 0);

    expect(storage.get('missing'), isNull);

    storage.set('key1', Uint8List.fromList([1, 2, 3]));
    expect(storage.get('key1'), equals(Uint8List.fromList([1, 2, 3])));
    expect(storage.size(), 1);

    storage.set('key1', Uint8List.fromList([9]));
    expect(storage.get('key1'), equals(Uint8List.fromList([9])));
    expect(storage.size(), 1);

    storage.set('key2', Uint8List.fromList([4, 5]));
    expect(storage.size(), 2);

    storage.delete('key1');
    expect(storage.get('key1'), isNull);
    expect(storage.size(), 1);

    storage.clear();
    expect(storage.size(), 0);
  });
}
