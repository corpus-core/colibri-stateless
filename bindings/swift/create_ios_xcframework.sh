#!/bin/bash

# iOS XCFramework Creation Script (Device + Simulator)
# Copyright (c) 2025 corpus.core

set -e

if [[ $# -lt 4 ]]; then
    echo "Usage: $0 <ios_arm_dir> <ios_x86_sim_dir> <header_file> <modulemap_file> [<ios_arm_sim_dir>]"
    exit 1
fi

IOS_ARM_BUILD="$1"
IOS_X86_BUILD="$2" 
HEADER_FILE="$3"
MODULEMAP_FILE="$4"
IOS_ARM_SIM_BUILD="${5:-}"

if [[ -n "$IOS_ARM_SIM_BUILD" ]]; then
    echo "🚀 Erstelle iOS XCFramework (Device arm64 + Simulator arm64/x86_64)..."
    echo "📱 iOS arm64 Device: $IOS_ARM_BUILD"
    echo "📱 iOS x86_64 Simulator: $IOS_X86_BUILD"
    echo "📱 iOS arm64 Simulator: $IOS_ARM_SIM_BUILD"
else
    echo "🚀 Erstelle iOS XCFramework (Device + Simulator x86_64 only)..."
    echo "📱 iOS arm64: $IOS_ARM_BUILD"
    echo "📱 iOS x86_64 Simulator: $IOS_X86_BUILD"
fi

# Libraries to combine (iOS-only with OP Stack support)
LIBRARIES=(
    "libs/crypto/libcrypto.a"
    "libs/blst/libblst.a" 
    "src/util/libutil.a"
    "src/prover/libprover.a"
    "src/verifier/libverifier.a"
    "bindings/swift/libc4_swift_binding.a"
    # ETH Chain support
    "src/chains/eth/libeth_verifier.a"
    "src/chains/eth/libeth_prover.a"
    "src/chains/eth/precompiles/libeth_precompiles.a"
    "src/chains/eth/bn254/libeth_bn254.a"
    "src/chains/eth/zk_verifier/libzk_verifier.a"
    # OP Stack support (NEW - these were missing!)
    "src/chains/op/libop_verifier.a"
    "src/chains/op/libop_prover.a"
    # EVMOne dependencies (always included as EVMONE is enabled by default)
    "libs/intx/libintx_wrapper.a"
    "libs/evmone/libevmone_wrapper.a"
    "_deps/evmone_external-build/libevmone.a"
    "libs/evmone/libkeccak_bridge.a"
    # ZSTD support for OP Stack preconfirmations
    "libs/zstd/zstd_build/lib/libzstd.a"
)

# Framework directories
IOS_ARM_FRAMEWORK="$IOS_ARM_BUILD/framework/ios-arm64/c4_swift.framework"

if [[ -n "$IOS_ARM_SIM_BUILD" ]]; then
    IOS_SIM_FRAMEWORK="$IOS_ARM_BUILD/framework/ios-arm64_x86_64-simulator/c4_swift.framework"
else
    IOS_SIM_FRAMEWORK="$IOS_X86_BUILD/framework/ios-x86_64-simulator/c4_swift.framework"
fi

# Create framework directories
echo "📁 Erstelle Framework-Strukturen..."
mkdir -p "$IOS_ARM_FRAMEWORK"/{Headers,Modules}
mkdir -p "$IOS_SIM_FRAMEWORK"/{Headers,Modules}

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

# Create combined libraries for iOS platforms
create_combined_library "$IOS_ARM_BUILD" "$IOS_ARM_FRAMEWORK" "iOS arm64"

if [[ -n "$IOS_ARM_SIM_BUILD" ]]; then
    # Build separate static libs for each simulator arch, then merge with lipo
    TMP_X86_FRAMEWORK="$IOS_X86_BUILD/framework/tmp-x86_64/c4_swift.framework"
    TMP_ARM_SIM_FRAMEWORK="$IOS_ARM_SIM_BUILD/framework/tmp-arm64-sim/c4_swift.framework"
    mkdir -p "$TMP_X86_FRAMEWORK"
    mkdir -p "$TMP_ARM_SIM_FRAMEWORK"
    create_combined_library "$IOS_X86_BUILD" "$TMP_X86_FRAMEWORK" "iOS x86_64 Simulator (partial)"
    create_combined_library "$IOS_ARM_SIM_BUILD" "$TMP_ARM_SIM_FRAMEWORK" "iOS arm64 Simulator (partial)"

    echo "🔗 Erstelle Fat Binary (arm64 + x86_64) für Simulator..."
    lipo -create \
        "$TMP_X86_FRAMEWORK/c4_swift" \
        "$TMP_ARM_SIM_FRAMEWORK/c4_swift" \
        -output "$IOS_SIM_FRAMEWORK/c4_swift"
    lipo -info "$IOS_SIM_FRAMEWORK/c4_swift"
else
    create_combined_library "$IOS_X86_BUILD" "$IOS_SIM_FRAMEWORK" "iOS x86_64 Simulator"
fi

# Copy headers and module maps to all frameworks
echo "📄 Kopiere Headers und Module Maps..."
for framework in "$IOS_ARM_FRAMEWORK" "$IOS_SIM_FRAMEWORK"; do
    cp "$HEADER_FILE" "$framework/Headers/"
    cp "$MODULEMAP_FILE" "$framework/Modules/"
done

# Create Info.plist files
echo "📋 Erstelle Info.plist Dateien..."

create_info_plist() {
    local framework_dir="$1"
    local platform_variant="$2"  # iPhoneOS or iPhoneSimulator
    local min_version="$3"
    
    # Delete existing plist if it exists
    rm -f "$framework_dir/Info.plist"
    
    /usr/libexec/PlistBuddy -c "Add :CFBundlePackageType string FMWK" \
        -c "Add :CFBundleIdentifier string com.c4.swift" \
        -c "Add :CFBundleName string c4_swift" \
        -c "Add :CFBundleVersion string 1.0" \
        -c "Add :CFBundleShortVersionString string 1.0" \
        -c "Add :MinimumOSVersion string $min_version" \
        -c "Add :CFBundleExecutable string c4_swift" \
        -c "Add :CFBundleSupportedPlatforms array" \
        -c "Add :CFBundleSupportedPlatforms:0 string $platform_variant" \
        "$framework_dir/Info.plist"
}

create_info_plist "$IOS_ARM_FRAMEWORK" "iPhoneOS" "13.0"
create_info_plist "$IOS_SIM_FRAMEWORK" "iPhoneSimulator" "13.0"

# Create iOS XCFramework
XCFRAMEWORK_PATH="$IOS_ARM_BUILD/c4_swift.xcframework"
echo "🎯 Erstelle iOS XCFramework: $XCFRAMEWORK_PATH"
rm -rf "$XCFRAMEWORK_PATH"

xcodebuild -create-xcframework \
    -framework "$IOS_ARM_FRAMEWORK" \
    -framework "$IOS_SIM_FRAMEWORK" \
    -output "$XCFRAMEWORK_PATH"

echo "🎉 iOS XCFramework erfolgreich erstellt!"
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

echo ""
echo "📝 Hinweis: Dies ist ein iOS-only XCFramework (Device arm64 + Simulator arm64/x86_64)"
echo "   Für macOS Support muss das CMake iOS-Detection Problem behoben werden."