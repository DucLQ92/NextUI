#!/usr/bin/env bash
set -e

# Default settings
DEFAULT_PLATFORM="tg5040"
PLATFORM="$DEFAULT_PLATFORM"
ACTION="system"
CORE=""

# Color definitions
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

usage() {
    cat << EOF
NextUI Build Helper Script

Usage:
  ./build.sh [ACTION] [OPTIONS]

Actions:
  system (default)     Build system binaries (nextui, minarch, settings, daemons)
  core <name>          Build a specific emulator core (e.g., ./build.sh core gambatte)
  cores                Build all emulator cores
  package              Build and package into release zip files in ./releases
  shell                Enter interactive bash shell in the Docker toolchain
  clean                Clean build artifacts
  list-cores           List all available cores for the selected platform

Options:
  -p, --platform <str> Target platform (default: tg5040) [tg5040 | tg5050 | desktop]
  -h, --help           Display this help guide

Examples:
  ./build.sh                   # Build system for tg5040 (Trimui Brick Pro / Smart Pro)
  ./build.sh core mgba         # Build GBA (mgba) core
  ./build.sh core gambatte     # Build GameBoy (gambatte) core
  ./build.sh package           # Build and package full release
  ./build.sh shell             # Enter Docker environment
  ./build.sh clean             # Clean artifacts
  ./build.sh -p tg5050 system  # Build system for tg5050
EOF
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        system|clean|shell|cores|package|list-cores)
            ACTION="$1"
            shift
            ;;
        core)
            ACTION="core"
            shift
            if [[ -n "$1" && "$1" != -* ]]; then
                CORE="$1"
                shift
            else
                echo -e "${RED}Error: 'core' action requires a core name (e.g. ./build.sh core gambatte)${NC}"
                exit 1
            fi
            ;;
        -p|--platform)
            PLATFORM="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo -e "${RED}Unknown option or action: $1${NC}"
            usage
            ;;
    esac
done

echo -e "${BLUE}==>${NC} Target Platform: ${GREEN}${PLATFORM}${NC} | Action: ${YELLOW}${ACTION}${NC}"

case "$ACTION" in
    system)
        echo -e "${BLUE}==>${NC} Building NextUI system binaries for ${PLATFORM}..."
        make build PLATFORM="${PLATFORM}"
        echo -e "${GREEN}✔ System binaries built successfully!${NC}"
        echo -e "${BLUE}ℹ Binaries located at:${NC} workspace/all/*/build/${PLATFORM}/ and workspace/${PLATFORM}/*/build/${PLATFORM}/"
        echo -e "${YELLOW}ℹ Note:${NC} To create release .zip files in ./releases, run: ${GREEN}./build.sh package${NC}"
        ;;
    core)
        echo -e "${BLUE}==>${NC} Building core '${CORE}' for ${PLATFORM}..."
        make build-core PLATFORM="${PLATFORM}" CORE="${CORE}"
        echo -e "${GREEN}✔ Core '${CORE}' built successfully!${NC}"
        ;;
    cores)
        echo -e "${BLUE}==>${NC} Building ALL cores for ${PLATFORM}..."
        make build-cores PLATFORM="${PLATFORM}"
        echo -e "${GREEN}✔ All cores built successfully!${NC}"
        ;;
    package)
        echo -e "${BLUE}==>${NC} Building and packaging release for ${PLATFORM} into ./releases..."
        mkdir -p ./releases
        make setup PLATFORMS="${PLATFORM}" "${PLATFORM}" special package done
        echo -e "${GREEN}✔ Release packages created successfully in ./releases!${NC}"
        ls -lh ./releases
        ;;
    shell)
        echo -e "${BLUE}==>${NC} Entering Docker toolchain shell for ${PLATFORM}..."
        make shell PLATFORM="${PLATFORM}"
        ;;
    clean)
        echo -e "${YELLOW}==>${NC} Cleaning build artifacts for ${PLATFORM}..."
        make clean PLATFORM="${PLATFORM}"
        echo -e "${GREEN}✔ Clean completed!${NC}"
        ;;
    list-cores)
        echo -e "${BLUE}==>${NC} Supported cores for ${PLATFORM}:"
        make cores-json PLATFORM="${PLATFORM}"
        ;;
esac
