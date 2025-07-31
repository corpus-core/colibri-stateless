#!/bin/bash

# XCFramework Creation Script
# Copyright (c) 2025 corpus.core
# SPDX-License-Identifier: MIT

set -e

# Arguments
BUILD_DIR="$1"
X86_BUILD_DIR="$2"
HEADER_FILE="$3"
MODULE_MAP="$4"
shift 4
LIBRARIES=("$@")

echo "🏗️  Creating XCFramework..."
echo "   Build dir: $BUILD_DIR"
echo "   x86 dir: $X86_BUILD_DIR"
echo "   Header: $HEADER_FILE"
echo "   Module map: $MODULE_MAP"
echo "   Libraries: ${LIBRARIES[@]}"

# Framework paths
DEVICE_FW="$BUILD_DIR/framework/ios-device/c4_swift.framework"
SIMULATOR_FW="$BUILD_DIR/framework/ios-simulator/c4_swift.framework"

# Create framework structure
mkdir -p "$DEVICE_FW"/{Headers,Modules}
mkdir -p "$SIMULATOR_FW"/{Headers,Modules}

# Copy headers and module map
cp "$HEADER_FILE" "$DEVICE_FW/Headers/"
cp "$HEADER_FILE" "$SIMULATOR_FW/Headers/"
cp "$MODULE_MAP" "$DEVICE_FW/Modules/"
cp "$MODULE_MAP" "$SIMULATOR_FW/Modules/"

# Collect library files for each architecture
DEVICE_LIBS=()
SIMULATOR_LIBS=()

for lib in "${LIBRARIES[@]}"; do
    device_lib="$BUILD_DIR/$lib"
    simulator_lib="$X86_BUILD_DIR/$lib"
    
    if [[ -f "$device_lib" ]]; then
        DEVICE_LIBS+=("$device_lib")
    else
        echo "⚠️  Warning: Device library not found: $device_lib"
    fi
    
    if [[ -f "$simulator_lib" ]]; then
        SIMULATOR_LIBS+=("$simulator_lib")
    else
        echo "⚠️  Warning: Simulator library not found: $simulator_lib"
    fi
done

# Create fat libraries using libtool
echo "📦 Creating device framework binary..."
libtool -static -o "$DEVICE_FW/c4_swift" "${DEVICE_LIBS[@]}"

echo "📦 Creating simulator framework binary..."
libtool -static -o "$SIMULATOR_FW/c4_swift" "${SIMULATOR_LIBS[@]}"

# Create Info.plist files
create_info_plist() {
    local plist_path="$1"
    cat > "$plist_path" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleIdentifier</key>
    <string>com.c4.swift</string>
    <key>CFBundleName</key>
    <string>c4_swift</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleExecutable</key>
    <string>c4_swift</string>
    <key>MinimumOSVersion</key>
    <string>13.0</string>
</dict>
</plist>
EOF
}

create_info_plist "$DEVICE_FW/Info.plist"
create_info_plist "$SIMULATOR_FW/Info.plist"

# Create XCFramework
echo "🔨 Creating XCFramework..."
xcodebuild -create-xcframework \
    -framework "$DEVICE_FW" \
    -framework "$SIMULATOR_FW" \
    -output "$BUILD_DIR/c4_swift.xcframework"

echo "✅ XCFramework created successfully: $BUILD_DIR/c4_swift.xcframework"

# Show info
echo "📊 Framework info:"
file "$BUILD_DIR/c4_swift.xcframework/ios-arm64/c4_swift.framework/c4_swift"
file "$BUILD_DIR/c4_swift.xcframework/ios-x86_64-simulator/c4_swift.framework/c4_swift"
echo "📏 Size: $(du -sh "$BUILD_DIR/c4_swift.xcframework" | cut -f1)"