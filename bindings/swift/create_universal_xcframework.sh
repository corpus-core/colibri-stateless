#!/bin/bash

# Universal XCFramework Creation Script (iOS + macOS)
# Copyright (c) 2025 corpus.core

set -e

if [[ $# -lt 6 ]]; then
    echo "Usage: $0 <ios_arm_dir> <ios_x86_dir> <macos_arm_dir> <macos_x86_dir> <header_file> <modulemap_file>"
    exit 1
fi

IOS_ARM_BUILD="$1"
IOS_X86_BUILD="$2" 
MACOS_ARM_BUILD="$3"
MACOS_X86_BUILD="$4"
HEADER_FILE="$5"
MODULEMAP_FILE="$6"

echo "🚀 Erstelle Universal XCFramework..."
echo "📱 iOS arm64: $IOS_ARM_BUILD"
echo "📱 iOS x86_64: $IOS_X86_BUILD"  
echo "💻 macOS arm64: $MACOS_ARM_BUILD"
echo "💻 macOS x86_64: $MACOS_X86_BUILD"

# Libraries to combine (without evmone for now to keep it simple)
LIBRARIES=(
    "libs/crypto/libcrypto.a"
    "libs/blst/libblst.a"
    "src/util/libutil.a"
    "src/proofer/libproofer.a"
    "src/chains/eth/libeth_verifier.a"
    "src/chains/eth/libeth_proofer.a"
    "src/verifier/libverifier.a"
    "bindings/swift/libc4_swift_binding.a"
)

# Framework directories
IOS_ARM_FRAMEWORK="$IOS_ARM_BUILD/framework/ios-arm64/c4_swift.framework"
IOS_X86_FRAMEWORK="$IOS_X86_BUILD/framework/ios-x86_64-simulator/c4_swift.framework"
MACOS_ARM_FRAMEWORK="$MACOS_ARM_BUILD/framework/macos-arm64/c4_swift.framework"
MACOS_X86_FRAMEWORK="$MACOS_X86_BUILD/framework/macos-x86_64/c4_swift.framework"

# Create framework directories
echo "📁 Erstelle Framework-Strukturen..."
mkdir -p "$IOS_ARM_FRAMEWORK"/{Headers,Modules}
mkdir -p "$IOS_X86_FRAMEWORK"/{Headers,Modules}
mkdir -p "$MACOS_ARM_FRAMEWORK"/{Headers,Modules}
mkdir -p "$MACOS_X86_FRAMEWORK"/{Headers,Modules}

# Function to create combined library for each platform
create_combined_library() {
    local build_dir="$1"
    local framework_dir="$2"
    local platform_name="$3"
    
    echo "🔨 Kombiniere Libraries für $platform_name..."
    
    # Collect all library files
    local lib_files=()
    for lib in "${LIBRARIES[@]}"; do
        local lib_path="$build_dir/$lib"
        if [[ -f "$lib_path" ]]; then
            lib_files+=("$lib_path")
        else
            echo "⚠️  Warning: Library nicht gefunden: $lib_path"
        fi
    done
    
    if [[ ${#lib_files[@]} -eq 0 ]]; then
        echo "❌ Fehler: Keine Libraries für $platform_name gefunden!"
        return 1
    fi
    
    # Combine libraries using libtool
    echo "📦 Kombiniere ${#lib_files[@]} Libraries für $platform_name..."
    libtool -static -o "$framework_dir/c4_swift" "${lib_files[@]}"
    
    echo "✅ $platform_name Library erstellt: $(file "$framework_dir/c4_swift" | cut -d: -f2)"
}

# Create combined libraries for each platform
create_combined_library "$IOS_ARM_BUILD" "$IOS_ARM_FRAMEWORK" "iOS arm64"
create_combined_library "$IOS_X86_BUILD" "$IOS_X86_FRAMEWORK" "iOS x86_64 Simulator"  
create_combined_library "$MACOS_ARM_BUILD" "$MACOS_ARM_FRAMEWORK" "macOS arm64"
create_combined_library "$MACOS_X86_BUILD" "$MACOS_X86_FRAMEWORK" "macOS x86_64"

# Copy headers and module maps to all frameworks
echo "📄 Kopiere Headers und Module Maps..."
for framework in "$IOS_ARM_FRAMEWORK" "$IOS_X86_FRAMEWORK" "$MACOS_ARM_FRAMEWORK" "$MACOS_X86_FRAMEWORK"; do
    cp "$HEADER_FILE" "$framework/Headers/"
    cp "$MODULEMAP_FILE" "$framework/Modules/"
done

# Create Info.plist files
echo "📋 Erstelle Info.plist Dateien..."

create_info_plist() {
    local framework_dir="$1"
    local platform="$2"
    local min_version="$3"
    
    /usr/libexec/PlistBuddy -c "Add :CFBundlePackageType string FMWK" \
        -c "Add :CFBundleIdentifier string com.c4.swift" \
        -c "Add :CFBundleName string c4_swift" \
        -c "Add :CFBundleVersion string 1.0" \
        -c "Add :CFBundleShortVersionString string 1.0" \
        -c "Add :MinimumOSVersion string $min_version" \
        -c "Add :CFBundleExecutable string c4_swift" \
        "$framework_dir/Info.plist"
}

create_info_plist "$IOS_ARM_FRAMEWORK" "iOS" "13.0"
create_info_plist "$IOS_X86_FRAMEWORK" "iOS" "13.0"
create_info_plist "$MACOS_ARM_FRAMEWORK" "macOS" "10.15"
create_info_plist "$MACOS_X86_FRAMEWORK" "macOS" "10.15"

# Create Universal XCFramework
XCFRAMEWORK_PATH="$IOS_ARM_BUILD/c4_swift.xcframework"
echo "🎯 Erstelle Universal XCFramework: $XCFRAMEWORK_PATH"

xcodebuild -create-xcframework \
    -framework "$IOS_ARM_FRAMEWORK" \
    -framework "$IOS_X86_FRAMEWORK" \
    -framework "$MACOS_ARM_FRAMEWORK" \
    -framework "$MACOS_X86_FRAMEWORK" \
    -output "$XCFRAMEWORK_PATH"

echo "🎉 Universal XCFramework erfolgreich erstellt!"
echo "📁 Pfad: $XCFRAMEWORK_PATH"

# Show what platforms are included
echo "🏗️  Unterstützte Platformen:"
if [[ -d "$XCFRAMEWORK_PATH" ]]; then
    find "$XCFRAMEWORK_PATH" -name "*.framework" | while read framework; do
        platform=$(basename "$(dirname "$framework")")
        binary="$framework/c4_swift"
        if [[ -f "$binary" ]]; then
            echo "  ✅ $platform: $(file "$binary" | cut -d: -f2-)"
        fi
    done
fi