/// Import typed byte arrays for storage values.
import 'dart:typed_data';

/// Import the Colibri storage interface implementation.
import 'package:colibri_stateless/colibri.dart';

/// Import the Dart test framework.
import 'package:test/test.dart';

/// Unit tests for the in-memory storage implementation.
void main() {
  /// Validate basic CRUD behavior and size accounting.
  test('MemoryStorage basic operations', () {
    /// Create a new storage instance with no entries.
    final storage = MemoryStorage();
    /// A new storage should be empty.
    expect(storage.size(), 0);

    /// Reading a missing key returns null.
    expect(storage.get('missing'), isNull);

    /// Store a small byte payload and verify it is returned.
    storage.set('key1', Uint8List.fromList([1, 2, 3]));
    expect(storage.get('key1'), equals(Uint8List.fromList([1, 2, 3])));
    /// Size increments after insert.
    expect(storage.size(), 1);

    /// Overwriting an existing key should replace the value.
    storage.set('key1', Uint8List.fromList([9]));
    expect(storage.get('key1'), equals(Uint8List.fromList([9])));
    /// Size stays the same when overwriting.
    expect(storage.size(), 1);

    /// Add a second key to validate multiple entries.
    storage.set('key2', Uint8List.fromList([4, 5]));
    expect(storage.size(), 2);

    /// Delete should remove the key and shrink the size.
    storage.delete('key1');
    expect(storage.get('key1'), isNull);
    expect(storage.size(), 1);

    /// Clearing wipes all entries.
    storage.clear();
    expect(storage.size(), 0);
  });
}
