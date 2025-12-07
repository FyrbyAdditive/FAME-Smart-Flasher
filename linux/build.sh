#!/bin/bash
# FAME Smart Flasher - Linux Build Script
# Copyright 2025 Fyrby Additive Manufacturing & Engineering

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
FLATPAK_BUILD_DIR="${SCRIPT_DIR}/flatpak-build"
APP_ID="com.fyrbyadditive.fame-smart-flasher"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${GREEN}[*]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

show_help() {
    echo "FAME Smart Flasher Build Script"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  build       Build the application (default)"
    echo "  flatpak     Build Flatpak package"
    echo "  clean       Clean build directories"
    echo "  install     Install dependencies (requires sudo)"
    echo "  run         Build and run the application"
    echo "  help        Show this help message"
    echo ""
}

install_dependencies() {
    print_status "Installing build dependencies..."

    if command -v apt-get &> /dev/null; then
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            cmake \
            ninja-build \
            qt6-base-dev \
            qt6-tools-dev \
            libudev-dev \
            flatpak \
            flatpak-builder
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y \
            gcc-c++ \
            cmake \
            ninja-build \
            qt6-qtbase-devel \
            systemd-devel \
            flatpak \
            flatpak-builder
    elif command -v pacman &> /dev/null; then
        sudo pacman -S --noconfirm \
            base-devel \
            cmake \
            ninja \
            qt6-base \
            systemd-libs \
            flatpak \
            flatpak-builder
    else
        print_error "Unsupported package manager. Please install dependencies manually:"
        echo "  - CMake"
        echo "  - Ninja"
        echo "  - Qt6 Base Development"
        echo "  - libudev development"
        echo "  - Flatpak and flatpak-builder"
        exit 1
    fi

    # Install Flatpak runtime and SDK
    print_status "Installing Flatpak runtime and SDK..."
    flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo || true
    flatpak install -y flathub org.kde.Platform//6.7 org.kde.Sdk//6.7 || true

    print_status "Dependencies installed successfully!"
}

build_app() {
    print_status "Building FAME Smart Flasher..."

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        "${SCRIPT_DIR}"

    ninja

    print_status "Build complete! Binary: ${BUILD_DIR}/fame-smart-flasher"
}

build_flatpak() {
    print_status "Building Flatpak package..."

    cd "${SCRIPT_DIR}"

    # Clean previous build
    rm -rf "${FLATPAK_BUILD_DIR}"
    rm -f "${SCRIPT_DIR}/${APP_ID}.flatpak"

    # Build the Flatpak
    flatpak-builder \
        --force-clean \
        --repo=repo \
        "${FLATPAK_BUILD_DIR}" \
        "flatpak/${APP_ID}.yml"

    # Create distributable bundle
    flatpak build-bundle \
        repo \
        "${APP_ID}.flatpak" \
        "${APP_ID}"

    print_status "Flatpak package created: ${SCRIPT_DIR}/${APP_ID}.flatpak"
    echo ""
    echo "To install the Flatpak:"
    echo "  flatpak install ${APP_ID}.flatpak"
    echo ""
    echo "To run the application:"
    echo "  flatpak run ${APP_ID}"
}

clean_build() {
    print_status "Cleaning build directories..."

    rm -rf "${BUILD_DIR}"
    rm -rf "${FLATPAK_BUILD_DIR}"
    rm -rf "${SCRIPT_DIR}/repo"
    rm -rf "${SCRIPT_DIR}/.flatpak-builder"
    rm -f "${SCRIPT_DIR}/${APP_ID}.flatpak"

    print_status "Clean complete!"
}

run_app() {
    build_app

    print_status "Running FAME Smart Flasher..."
    "${BUILD_DIR}/fame-smart-flasher"
}

# Main script
case "${1:-build}" in
    build)
        build_app
        ;;
    flatpak)
        build_flatpak
        ;;
    clean)
        clean_build
        ;;
    install)
        install_dependencies
        ;;
    run)
        run_app
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        print_error "Unknown command: $1"
        show_help
        exit 1
        ;;
esac
