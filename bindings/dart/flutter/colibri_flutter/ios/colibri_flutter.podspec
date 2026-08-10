Pod::Spec.new do |s|
  s.name             = 'colibri_flutter'
  s.version          = '0.1.0'
  s.summary          = 'Flutter wrapper for Colibri Stateless (FFI).'
  s.description      = 'Dart FFI wrapper for Colibri Stateless with bundled native binaries.'
  s.homepage         = 'https://github.com/corpus-core/colibri-stateless'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'corpus.core' => 'info@corpuscore.tech' }
  s.source           = { :path => '.' }
  # Shared with Swift Package Manager under ios/colibri_flutter/
  s.source_files     = 'colibri_flutter/Sources/colibri_flutter/**/*.swift',
                       'colibri_flutter/Sources/colibri_force_link/**/*.{c,h}'
  s.public_header_files = 'colibri_flutter/Sources/colibri_force_link/**/*.h'
  s.platform         = :ios, '13.0'
  s.swift_version    = '5.0'
  # Static archive inside the XCFramework. With Flutter `use_frameworks!` this
  # pod becomes a dynamic framework; -force_load pulls every object file so
  # Dart FFI symbols are present even before force_link.c references resolve.
  s.vendored_frameworks = 'colibri_flutter/Frameworks/c4_swift.xcframework'
  s.dependency 'Flutter'
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'OTHER_LDFLAGS' => '$(inherited) -force_load "$(PODS_XCFRAMEWORKS_BUILD_DIR)/colibri_flutter/c4_swift.framework/c4_swift"',
  }
end
