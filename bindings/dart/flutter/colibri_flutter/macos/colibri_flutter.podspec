Pod::Spec.new do |s|
  s.name             = 'colibri_flutter'
  s.version          = '0.1.9'
  s.summary          = 'Flutter wrapper for Colibri Stateless (FFI).'
  s.description      = 'Dart FFI wrapper for Colibri Stateless with bundled native binaries (Android, iOS, macOS, Linux).'
  s.homepage         = 'https://github.com/corpus-core/colibri-stateless'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'corpus.core' => 'info@corpuscore.tech' }
  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.platform         = :osx, '10.15'
  s.swift_version    = '5.0'
  s.vendored_libraries = 'Frameworks/libcolibri.dylib'
  s.preserve_paths   = 'Frameworks/libcolibri.dylib'
  s.xcconfig         = { 'LD_RUNPATH_SEARCH_PATHS' => '@loader_path/../Frameworks' }
end
