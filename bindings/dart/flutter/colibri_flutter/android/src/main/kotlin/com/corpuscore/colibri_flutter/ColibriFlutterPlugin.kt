package com.corpuscore.colibri_flutter

import io.flutter.embedding.engine.plugins.FlutterPlugin

class ColibriFlutterPlugin : FlutterPlugin {

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        // Load libcolibri.so from jniLibs so Dart FFI can resolve symbols via DynamicLibrary.process().
        System.loadLibrary("colibri")
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {}
}
