#!/bin/bash

# ==============================================================================
# Script: ensure_image.sh
# Description: Ensures the Docker image exists for the current platform. 
#              Builds it if missing.
#
# Platform Detection:
#   - Automatically detects ARM64 vs AMD64 architecture
#   - Uses Docker's TARGETARCH for cross-platform builds
#
# Usage:
#   ./ensure_image.sh [--rebuild]
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

check_docker

REBUILD=false
if [[ "$1" == "--rebuild" ]]; then
    REBUILD=true
fi

PLATFORM_ARCH=$(get_platform_arch)
echo "=== Docker Image Verification ==="
echo "Platform: $PLATFORM_ARCH"
echo "Image:    $DOCKER_FULL_IMAGE"
echo "================================="

if docker_image_exists && [[ "$REBUILD" == "false" ]]; then
    echo "✓ Docker image already exists: $DOCKER_FULL_IMAGE"
    
    # Verify architecture matches
    IMAGE_ARCH=$(docker image inspect "$DOCKER_FULL_IMAGE" --format '{{.Architecture}}' 2>/dev/null || echo "unknown")
    if [[ "$IMAGE_ARCH" == "$PLATFORM_ARCH" ]]; then
        echo "✓ Architecture matches: $IMAGE_ARCH"
        exit 0
    else
        echo "! Image architecture ($IMAGE_ARCH) doesn't match platform ($PLATFORM_ARCH)"
        echo "  Rebuilding image..."
        REBUILD=true
    fi
fi

if [[ "$REBUILD" == "true" ]] || ! docker_image_exists; then
    echo "Building Docker image for $PLATFORM_ARCH..."
    echo "Dockerfile: $DOCKERFILE_PATH"
    echo "This may take 5-15 minutes..."
    echo ""
    
    BUILD_START=$(date +%s)
    
    # Build with platform-specific settings
    docker build \
        --platform "linux/$PLATFORM_ARCH" \
        -t "$DOCKER_FULL_IMAGE" \
        -f "$DOCKERFILE_PATH" \
        "${PROJECT_ROOT}/docker"
    
    BUILD_END=$(date +%s)
    BUILD_DURATION=$((BUILD_END - BUILD_START))
    
    echo ""
    echo "✓ Docker image built successfully in $(format_duration $BUILD_DURATION)"
    echo "  Image: $DOCKER_FULL_IMAGE"
fi
