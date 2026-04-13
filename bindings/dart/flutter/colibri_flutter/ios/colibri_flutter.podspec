Pod::Spec.new do |s|
  s.name             = 'colibri_flutter'
  s.version          = '0.1.9'
  s.summary          = 'Flutter wrapper for Colibri Stateless (FFI).'
  s.description      = 'Dart FFI wrapper for Colibri Stateless with bundled native binaries.'
  s.homepage         = 'https://github.com/corpus-core/colibri-stateless'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'corpus.core' => 'info@corpuscore.tech' }
  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.platform         = :ios, '13.0'
  s.swift_version    = '5.0'
  s.vendored_frameworks = 'Frameworks/c4_swift.xcframework'
  s.dependency 'Flutter'

  s.pod_target_xcconfig = {
    'OTHER_LDFLAGS[sdk=iphoneos*]' =>
      '$(inherited) -force_load "$(PODS_TARGET_SRCROOT)/Frameworks/c4_swift.xcframework/ios-arm64/c4_swift.framework/c4_swift"',
    'OTHER_LDFLAGS[sdk=iphonesimulator*]' =>
      '$(inherited) -force_load "$(PODS_TARGET_SRCROOT)/Frameworks/c4_swift.xcframework/ios-arm64_x86_64-simulator/c4_swift.framework/c4_swift"',
  }
end
