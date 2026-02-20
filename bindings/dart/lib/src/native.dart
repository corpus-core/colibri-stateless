import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'storage.dart';

final class BytesT extends ffi.Struct {
  @ffi.Uint32()
  external int len;

  external ffi.Pointer<ffi.Uint8> data;
}

final class BufferT extends ffi.Struct {
  external BytesT data;

  @ffi.Int32()
  external int allocated;
}

typedef _StorageGetNative = ffi.Uint8 Function(
  ffi.Pointer<ffi.Int8>,
  ffi.Pointer<BufferT>,
);
typedef _StorageSetNative = ffi.Void Function(
  ffi.Pointer<ffi.Int8>,
  BytesT,
);
typedef _StorageDeleteNative = ffi.Void Function(
  ffi.Pointer<ffi.Int8>,
);

final class StoragePluginT extends ffi.Struct {
  external ffi.Pointer<ffi.NativeFunction<_StorageGetNative>> get;
  external ffi.Pointer<ffi.NativeFunction<_StorageSetNative>> set;
  external ffi.Pointer<ffi.NativeFunction<_StorageDeleteNative>> del;

  @ffi.Uint32()
  external int maxSyncStates;
}

typedef _CreateProverCtxNative = ffi.Pointer<ffi.Void> Function(
  ffi.Pointer<ffi.Int8>,
  ffi.Pointer<ffi.Int8>,
  ffi.Uint64,
  ffi.Uint32,
);
typedef _CreateProverCtx = ffi.Pointer<ffi.Void> Function(
  ffi.Pointer<ffi.Int8>,
  ffi.Pointer<ffi.Int8>,
  int,
  int,
);

typedef _ExecuteStatusNative = ffi.Pointer<ffi.Int8> Function(ffi.Pointer<ffi.Void>);
typedef _ExecuteStatus = ffi.Pointer<ffi.Int8> Function(ffi.Pointer<ffi.Void>);

typedef _GetProofNative = BytesT Function(ffi.Pointer<ffi.Void>);
typedef _GetProof = BytesT Function(ffi.Pointer<ffi.Void>);

typedef _FreeCtxNative = ffi.Void Function(ffi.Pointer<ffi.Void>);
typedef _FreeCtx = void Function(ffi.Pointer<ffi.Void>);

typedef _ReqSetResponseNative = ffi.Void Function(
  ffi.Pointer<ffi.Void>,
  BytesT,
  ffi.Uint16,
);
typedef _ReqSetResponse = void Function(ffi.Pointer<ffi.Void>, BytesT, int);

typedef _ReqSetErrorNative = ffi.Void Function(
  ffi.Pointer<ffi.Void>,
  ffi.Pointer<ffi.Int8>,
  ffi.Uint16,
);
typedef _ReqSetError = void Function(ffi.Pointer<ffi.Void>, ffi.Pointer<ffi.Int8>, int);

typedef _VerifyCreateNative = ffi.Pointer<ffi.Void> Function(
  BytesT,
  ffi.Pointer<ffi.Int8>,
  ffi.Pointer<ffi.Int8>,
  ffi.Uint64,
  ffi.Pointer<ffi.Int8>,
);
typedef _VerifyCreate = ffi.Pointer<ffi.Void> Function(
  BytesT,
  ffi.Pointer<ffi.Int8>,
  ffi.Pointer<ffi.Int8>,
  int,
  ffi.Pointer<ffi.Int8>,
);

typedef _GetMethodSupportNative = ffi.Int32 Function(ffi.Uint64, ffi.Pointer<ffi.Int8>);
typedef _GetMethodSupport = int Function(int, ffi.Pointer<ffi.Int8>);

typedef _FreeNative = ffi.Void Function(ffi.Pointer<ffi.Void>);
typedef _Free = void Function(ffi.Pointer<ffi.Void>);

typedef _SetStorageConfigNative = ffi.Void Function(ffi.Pointer<StoragePluginT>);
typedef _SetStorageConfig = void Function(ffi.Pointer<StoragePluginT>);

typedef _BufferAppendNative = ffi.Uint32 Function(ffi.Pointer<BufferT>, BytesT);
typedef _BufferAppend = int Function(ffi.Pointer<BufferT>, BytesT);

ColibriStorage? _storageHandler;
late _BufferAppend _bufferAppend;

int _storageGet(ffi.Pointer<ffi.Int8> keyPtr, ffi.Pointer<BufferT> bufferPtr) {
  final storage = _storageHandler;
  if (storage == null) {
    return 0;
  }
  try {
    final key = keyPtr.cast<Utf8>().toDartString();
    final data = storage.get(key);
    if (data == null) {
      return 0;
    }
    if (data.isEmpty) {
      return 1;
    }
    final dataPtr = malloc<ffi.Uint8>(data.length);
    dataPtr.asTypedList(data.length).setAll(0, data);
    final bytes = calloc<BytesT>();
    bytes.ref
      ..len = data.length
      ..data = dataPtr;
    _bufferAppend(bufferPtr, bytes.ref);
    malloc.free(dataPtr);
    calloc.free(bytes);
    return 1;
  } catch (_) {
    return 0;
  }
}

void _storageSet(ffi.Pointer<ffi.Int8> keyPtr, BytesT value) {
  final storage = _storageHandler;
  if (storage == null) {
    return;
  }
  try {
    final key = keyPtr.cast<Utf8>().toDartString();
    final bytes = value.data == ffi.nullptr || value.len == 0
        ? Uint8List(0)
        : Uint8List.fromList(value.data.asTypedList(value.len));
    storage.set(key, bytes);
  } catch (_) {
    return;
  }
}

void _storageDelete(ffi.Pointer<ffi.Int8> keyPtr) {
  final storage = _storageHandler;
  if (storage == null) {
    return;
  }
  try {
    final key = keyPtr.cast<Utf8>().toDartString();
    storage.delete(key);
  } catch (_) {
    return;
  }
}

final ffi.Pointer<ffi.NativeFunction<_StorageGetNative>> _storageGetPtr =
    ffi.Pointer.fromFunction<_StorageGetNative>(_storageGet, 0);
final ffi.Pointer<ffi.NativeFunction<_StorageSetNative>> _storageSetPtr =
    ffi.Pointer.fromFunction<_StorageSetNative>(_storageSet);
final ffi.Pointer<ffi.NativeFunction<_StorageDeleteNative>> _storageDeletePtr =
    ffi.Pointer.fromFunction<_StorageDeleteNative>(_storageDelete);

class ColibriNative {
  ColibriNative._(this._lib, this._libc) {
    _createProverCtx = _lib.lookupFunction<_CreateProverCtxNative, _CreateProverCtx>(
      'c4_create_prover_ctx',
    );
    _proverExecuteJsonStatus =
        _lib.lookupFunction<_ExecuteStatusNative, _ExecuteStatus>('c4_prover_execute_json_status');
    _proverGetProof = _lib.lookupFunction<_GetProofNative, _GetProof>('c4_prover_get_proof');
    _freeProverCtx = _lib.lookupFunction<_FreeCtxNative, _FreeCtx>('c4_free_prover_ctx');
    _reqSetResponse =
        _lib.lookupFunction<_ReqSetResponseNative, _ReqSetResponse>('c4_req_set_response');
    _reqSetError = _lib.lookupFunction<_ReqSetErrorNative, _ReqSetError>('c4_req_set_error');
    _verifyCreateCtx =
        _lib.lookupFunction<_VerifyCreateNative, _VerifyCreate>('c4_verify_create_ctx');
    _verifyExecuteJsonStatus =
        _lib.lookupFunction<_ExecuteStatusNative, _ExecuteStatus>('c4_verify_execute_json_status');
    _verifyFreeCtx = _lib.lookupFunction<_FreeCtxNative, _FreeCtx>('c4_verify_free_ctx');
    _getMethodSupport =
        _lib.lookupFunction<_GetMethodSupportNative, _GetMethodSupport>('c4_get_method_support');
    _free = _libc.lookupFunction<_FreeNative, _Free>('free');
    _setStorageConfig =
        _lib.lookupFunction<_SetStorageConfigNative, _SetStorageConfig>('c4_set_storage_config');
    _bufferAppend = _lib.lookupFunction<_BufferAppendNative, _BufferAppend>('buffer_append');
  }

  final ffi.DynamicLibrary _lib;
  final ffi.DynamicLibrary _libc;

  late final _CreateProverCtx _createProverCtx;
  late final _ExecuteStatus _proverExecuteJsonStatus;
  late final _GetProof _proverGetProof;
  late final _FreeCtx _freeProverCtx;
  late final _ReqSetResponse _reqSetResponse;
  late final _ReqSetError _reqSetError;
  late final _VerifyCreate _verifyCreateCtx;
  late final _ExecuteStatus _verifyExecuteJsonStatus;
  late final _FreeCtx _verifyFreeCtx;
  late final _GetMethodSupport _getMethodSupport;
  late final _Free _free;
  late final _SetStorageConfig _setStorageConfig;

  static ColibriNative load({String? libraryPath}) {
    final resolvedPath = libraryPath ?? _resolveDefaultLibraryPath();
    final lib = ffi.DynamicLibrary.open(resolvedPath);
    final libc = _openLibc();
    return ColibriNative._(lib, libc);
  }

  ffi.Pointer<ffi.Void> createProverCtx(
    String method,
    String params,
    int chainId,
    int flags,
  ) {
    final methodPtr = method.toNativeUtf8();
    final paramsPtr = params.toNativeUtf8();
    final ctx = _createProverCtx(
      methodPtr.cast(),
      paramsPtr.cast(),
      chainId,
      flags,
    );
    malloc.free(methodPtr);
    malloc.free(paramsPtr);
    return ctx;
  }

  String proverExecuteJsonStatus(ffi.Pointer<ffi.Void> ctx) {
    final resultPtr = _proverExecuteJsonStatus(ctx);
    final result = resultPtr.cast<Utf8>().toDartString();
    _free(resultPtr.cast());
    return result;
  }

  Uint8List proverGetProof(ffi.Pointer<ffi.Void> ctx) {
    final proof = _proverGetProof(ctx);
    if (proof.len == 0 || proof.data == ffi.nullptr) {
      return Uint8List(0);
    }
    return Uint8List.fromList(proof.data.asTypedList(proof.len));
  }

  void freeProverCtx(ffi.Pointer<ffi.Void> ctx) => _freeProverCtx(ctx);

  ffi.Pointer<ffi.Void> verifyCreateCtx(
    Uint8List proof,
    String method,
    String args,
    int chainId,
    String trustedCheckpoint,
  ) {
    final proofPtr = proof.isEmpty ? ffi.nullptr : malloc<ffi.Uint8>(proof.length);
    if (proof.isNotEmpty) {
      proofPtr.asTypedList(proof.length).setAll(0, proof);
    }
    final bytes = calloc<BytesT>();
    bytes.ref
      ..len = proof.length
      ..data = proofPtr;

    final methodPtr = method.toNativeUtf8();
    final argsPtr = args.toNativeUtf8();
    final checkpointPtr = trustedCheckpoint.toNativeUtf8();

    final ctx = _verifyCreateCtx(
      bytes.ref,
      methodPtr.cast(),
      argsPtr.cast(),
      chainId,
      checkpointPtr.cast(),
    );

    if (proofPtr != ffi.nullptr) {
      malloc.free(proofPtr);
    }
    calloc.free(bytes);
    malloc.free(methodPtr);
    malloc.free(argsPtr);
    malloc.free(checkpointPtr);

    return ctx;
  }

  String verifyExecuteJsonStatus(ffi.Pointer<ffi.Void> ctx) {
    final resultPtr = _verifyExecuteJsonStatus(ctx);
    final result = resultPtr.cast<Utf8>().toDartString();
    _free(resultPtr.cast());
    return result;
  }

  void verifyFreeCtx(ffi.Pointer<ffi.Void> ctx) => _verifyFreeCtx(ctx);

  void reqSetResponse(int reqPtr, Uint8List data, int nodeIndex) {
    final dataPtr = data.isEmpty ? ffi.nullptr : malloc<ffi.Uint8>(data.length);
    if (data.isNotEmpty) {
      dataPtr.asTypedList(data.length).setAll(0, data);
    }
    final bytes = calloc<BytesT>();
    bytes.ref
      ..len = data.length
      ..data = dataPtr;

    _reqSetResponse(ffi.Pointer.fromAddress(reqPtr), bytes.ref, nodeIndex);
    if (dataPtr != ffi.nullptr) {
      malloc.free(dataPtr);
    }
    calloc.free(bytes);
  }

  void reqSetError(int reqPtr, String error, int nodeIndex) {
    final errorPtr = error.toNativeUtf8();
    _reqSetError(ffi.Pointer.fromAddress(reqPtr), errorPtr.cast(), nodeIndex);
    malloc.free(errorPtr);
  }

  int getMethodSupport(int chainId, String method) {
    final methodPtr = method.toNativeUtf8();
    final result = _getMethodSupport(chainId, methodPtr.cast());
    malloc.free(methodPtr);
    return result;
  }

  void registerStorage(ColibriStorage storage, {int maxSyncStates = 3}) {
    _storageHandler = storage;
    final plugin = calloc<StoragePluginT>();
    plugin.ref
      ..get = _storageGetPtr
      ..set = _storageSetPtr
      ..del = _storageDeletePtr
      ..maxSyncStates = maxSyncStates;
    _setStorageConfig(plugin);
    calloc.free(plugin);
  }

  void clearStorage() {
    _storageHandler = null;
    final plugin = calloc<StoragePluginT>();
    plugin.ref
      ..get = ffi.nullptr
      ..set = ffi.nullptr
      ..del = ffi.nullptr
      ..maxSyncStates = 0;
    _setStorageConfig(plugin);
    calloc.free(plugin);
  }

  static String _resolveDefaultLibraryPath() {
    final envPath = Platform.environment['COLIBRI_DART_LIBRARY'];
    if (envPath != null && envPath.isNotEmpty) {
      return envPath;
    }

    if (Platform.isMacOS) {
      return 'native/libcolibri.dylib';
    }
    if (Platform.isLinux) {
      return 'native/libcolibri.so';
    }
    if (Platform.isWindows) {
      return 'native/colibri.dll';
    }
    throw UnsupportedError('Unsupported platform for Colibri native library');
  }

  static ffi.DynamicLibrary _openLibc() {
    if (Platform.isMacOS) {
      return ffi.DynamicLibrary.open('/usr/lib/libc.dylib');
    }
    if (Platform.isLinux) {
      return ffi.DynamicLibrary.open('libc.so.6');
    }
    if (Platform.isWindows) {
      return ffi.DynamicLibrary.open('msvcrt.dll');
    }
    throw UnsupportedError('Unsupported platform for libc');
  }
}
