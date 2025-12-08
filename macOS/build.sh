#!/bin/bash
#
# FAME Smart Flasher - macOS Build Script
# Builds, signs, and creates a signed installer package
#
# Usage: ./build.sh [options]
#   --clean            Clean build directory before building
#   --help             Show this help message
#
# Required Environment Variables:
#   DEVELOPER_ID_APP       - Developer ID Application certificate name
#                            e.g., "Developer ID Application: Your Name (TEAMID)"
#   DEVELOPER_ID_INSTALLER - Developer ID Installer certificate name
#                            e.g., "Developer ID Installer: Your Name (TEAMID)"
#

set -e

# Configuration
APP_NAME="FAME Smart Flasher"
BUNDLE_ID="com.fyrbyadditive.fame-smart-flasher"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_FILE="$PROJECT_DIR/$APP_NAME.xcodeproj"
SCHEME="$APP_NAME"
BUILD_DIR="$PROJECT_DIR/build"
ARCHIVE_PATH="$BUILD_DIR/$APP_NAME.xcarchive"
EXPORT_PATH="$BUILD_DIR/export"
APP_PATH="$EXPORT_PATH/$APP_NAME.app"
PKG_PATH="$BUILD_DIR/$APP_NAME.pkg"
VERSION=$(grep -A1 "CFBundleShortVersionString" "$PROJECT_DIR/$APP_NAME/Info.plist" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
BUILD_NUMBER=$(grep -A1 "CFBundleVersion" "$PROJECT_DIR/$APP_NAME/Info.plist" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Options
CLEAN_BUILD=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --help)
            head -16 "$0" | tail -14
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Helper functions
log_step() {
    echo -e "\n${BLUE}==>${NC} ${GREEN}$1${NC}"
}

log_info() {
    echo -e "    ${YELLOW}$1${NC}"
}

log_error() {
    echo -e "${RED}Error: $1${NC}" >&2
}

check_requirements() {
    log_step "Checking requirements..."

    # Check Xcode
    if ! command -v xcodebuild &> /dev/null; then
        log_error "xcodebuild not found. Please install Xcode."
        exit 1
    fi

    # Check signing certificates
    if [[ -z "$DEVELOPER_ID_APP" ]]; then
        log_error "DEVELOPER_ID_APP environment variable not set"
        log_info "Example: export DEVELOPER_ID_APP=\"Developer ID Application: Your Name (TEAMID)\""
        log_info ""
        log_info "Available signing identities:"
        security find-identity -v -p codesigning | grep "Developer ID" || true
        exit 1
    fi

    if [[ -z "$DEVELOPER_ID_INSTALLER" ]]; then
        log_error "DEVELOPER_ID_INSTALLER environment variable not set"
        log_info "Example: export DEVELOPER_ID_INSTALLER=\"Developer ID Installer: Your Name (TEAMID)\""
        log_info ""
        log_info "Available installer identities:"
        security find-identity -v | grep "Developer ID Installer" || true
        exit 1
    fi

    # Verify certificates exist in keychain
    if ! security find-identity -v -p codesigning | grep -q "$DEVELOPER_ID_APP"; then
        log_error "Certificate not found: $DEVELOPER_ID_APP"
        log_info "Available signing identities:"
        security find-identity -v -p codesigning
        exit 1
    fi

    log_info "All requirements satisfied"
}

clean_build() {
    if [[ "$CLEAN_BUILD" == true ]]; then
        log_step "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
    fi
    mkdir -p "$BUILD_DIR"
}

build_archive() {
    log_step "Building archive..."
    log_info "Project: $PROJECT_FILE"
    log_info "Scheme: $SCHEME"
    log_info "Version: $VERSION ($BUILD_NUMBER)"

    if command -v xcpretty &> /dev/null; then
        xcodebuild archive \
            -project "$PROJECT_FILE" \
            -scheme "$SCHEME" \
            -configuration Release \
            -archivePath "$ARCHIVE_PATH" \
            -destination "generic/platform=macOS" \
            CODE_SIGN_IDENTITY="$DEVELOPER_ID_APP" \
            CODE_SIGN_STYLE=Manual \
            OTHER_CODE_SIGN_FLAGS="--timestamp --options=runtime" \
            | xcpretty
    else
        xcodebuild archive \
            -project "$PROJECT_FILE" \
            -scheme "$SCHEME" \
            -configuration Release \
            -archivePath "$ARCHIVE_PATH" \
            -destination "generic/platform=macOS" \
            CODE_SIGN_IDENTITY="$DEVELOPER_ID_APP" \
            CODE_SIGN_STYLE=Manual \
            OTHER_CODE_SIGN_FLAGS="--timestamp --options=runtime"
    fi

    if [[ $? -ne 0 ]]; then
        log_error "Archive failed"
        exit 1
    fi

    log_info "Archive created: $ARCHIVE_PATH"
}

export_app() {
    log_step "Exporting application..."

    # Create export options plist
    EXPORT_OPTIONS="$BUILD_DIR/ExportOptions.plist"
    cat > "$EXPORT_OPTIONS" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>developer-id</string>
    <key>signingStyle</key>
    <string>manual</string>
    <key>signingCertificate</key>
    <string>Developer ID Application</string>
</dict>
</plist>
EOF

    if command -v xcpretty &> /dev/null; then
        xcodebuild -exportArchive \
            -archivePath "$ARCHIVE_PATH" \
            -exportPath "$EXPORT_PATH" \
            -exportOptionsPlist "$EXPORT_OPTIONS" \
            | xcpretty
    else
        xcodebuild -exportArchive \
            -archivePath "$ARCHIVE_PATH" \
            -exportPath "$EXPORT_PATH" \
            -exportOptionsPlist "$EXPORT_OPTIONS"
    fi

    if [[ $? -ne 0 ]]; then
        log_error "Export failed"
        exit 1
    fi

    log_info "Application exported: $APP_PATH"
}

sign_app() {
    log_step "Signing application..."

    # Sign the app with hardened runtime and timestamp
    codesign --force --deep --timestamp \
        --options runtime \
        --sign "$DEVELOPER_ID_APP" \
        "$APP_PATH"

    # Verify signature
    log_info "Verifying signature..."
    codesign --verify --deep --strict --verbose=2 "$APP_PATH"

    log_info "Application signed successfully"
}

create_pkg() {
    log_step "Creating installer package..."

    # Create component plist
    COMPONENT_PLIST="$BUILD_DIR/component.plist"
    pkgbuild --analyze --root "$EXPORT_PATH" "$COMPONENT_PLIST"

    # Modify component plist to install to /Applications
    /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$COMPONENT_PLIST"

    # Build the component package
    COMPONENT_PKG="$BUILD_DIR/$APP_NAME-component.pkg"
    pkgbuild \
        --root "$EXPORT_PATH" \
        --component-plist "$COMPONENT_PLIST" \
        --install-location "/Applications" \
        --identifier "$BUNDLE_ID.pkg" \
        --version "$VERSION" \
        "$COMPONENT_PKG"

    # Create distribution XML
    DISTRIBUTION_XML="$BUILD_DIR/distribution.xml"
    cat > "$DISTRIBUTION_XML" << EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$APP_NAME</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <license file="license.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="never" require-scripts="false" hostArchitectures="x86_64,arm64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <choices-outline>
        <line choice="default">
            <line choice="$BUNDLE_ID.pkg"/>
        </line>
    </choices-outline>
    <choice id="default"/>
    <choice id="$BUNDLE_ID.pkg" visible="false">
        <pkg-ref id="$BUNDLE_ID.pkg"/>
    </choice>
    <pkg-ref id="$BUNDLE_ID.pkg" version="$VERSION" onConclusion="none">$APP_NAME-component.pkg</pkg-ref>
</installer-gui-script>
EOF

    # Create resources directory with installer pages
    RESOURCES_DIR="$BUILD_DIR/resources"
    mkdir -p "$RESOURCES_DIR"

    cat > "$RESOURCES_DIR/welcome.html" << EOF
<!DOCTYPE html>
<html>
<head>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; padding: 20px; }
        h1 { color: #333; }
        p { color: #666; line-height: 1.6; }
    </style>
</head>
<body>
    <h1>Welcome to $APP_NAME</h1>
    <p>This installer will guide you through the installation of $APP_NAME version $VERSION.</p>
    <p>$APP_NAME is a tool for flashing firmware to ESP32-C3 microcontrollers via USB.</p>
    <p>Click Continue to proceed with the installation.</p>
</body>
</html>
EOF

    cat > "$RESOURCES_DIR/license.html" << EOF
<!DOCTYPE html>
<html>
<head>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; padding: 20px; }
        h1 { color: #333; }
        p { color: #666; line-height: 1.6; }
    </style>
</head>
<body>
    <h1>License Agreement</h1>
    <p>Copyright © 2025 Fyrby Additive Manufacturing & Engineering</p>
    <p>All rights reserved.</p>
    <p>This software is provided for use with authorized devices only.</p>
</body>
</html>
EOF

    cat > "$RESOURCES_DIR/conclusion.html" << EOF
<!DOCTYPE html>
<html>
<head>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; padding: 20px; }
        h1 { color: #333; }
        p { color: #666; line-height: 1.6; }
        .success { color: #4CAF50; }
    </style>
</head>
<body>
    <h1 class="success">Installation Complete</h1>
    <p>$APP_NAME has been installed successfully.</p>
    <p>You can find it in your Applications folder.</p>
    <p>Connect your ESP32-C3 device via USB and launch the application to get started.</p>
</body>
</html>
EOF

    # Build the product archive (signed installer)
    productbuild \
        --distribution "$DISTRIBUTION_XML" \
        --resources "$RESOURCES_DIR" \
        --package-path "$BUILD_DIR" \
        --sign "$DEVELOPER_ID_INSTALLER" \
        --timestamp \
        "$PKG_PATH"

    # Verify package signature
    log_info "Verifying package signature..."
    pkgutil --check-signature "$PKG_PATH"

    log_info "Package created: $PKG_PATH"
}

print_summary() {
    log_step "Build complete!"
    echo ""
    echo -e "${GREEN}=== Build Summary ===${NC}"
    echo -e "  Package: ${BLUE}$PKG_PATH${NC}"
    echo -e "  Version: ${YELLOW}$VERSION ($BUILD_NUMBER)${NC}"
    echo ""
}

# Main execution
main() {
    echo -e "${GREEN}"
    echo "╔═══════════════════════════════════════════╗"
    echo "║     FAME Smart Flasher - Build Script     ║"
    echo "╚═══════════════════════════════════════════╝"
    echo -e "${NC}"

    check_requirements
    clean_build
    build_archive
    export_app
    sign_app
    create_pkg
    print_summary
}

main
