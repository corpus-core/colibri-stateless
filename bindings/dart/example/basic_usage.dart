import 'dart:io';

import 'package:colibri_stateless/colibri.dart';

Future<void> main() async {
  final libraryPath = Platform.isWindows
      ? 'native/colibri.dll'
      : Platform.isMacOS
          ? 'native/libcolibri.dylib'
          : 'native/libcolibri.so';

  final colibri = Colibri(chainId: 1, libraryPath: libraryPath);

  final blockNumber = await colibri.rpc('eth_blockNumber', []);
  print('Block number: $blockNumber');

  colibri.close();
}
