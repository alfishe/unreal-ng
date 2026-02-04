#!/bin/bash
set -e

# Determine project root by searching upward for CMakeLists.txt
find_project_root() {
    local current="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
    local max_depth=10
    local depth=0
    
    while [ $depth -lt $max_depth ]; do
        if [ -f "$current/CMakeLists.txt" ]; then
            # Verify it's the root CMakeLists.txt
            if grep -q "project(" "$current/CMakeLists.txt" 2>/dev/null; then
                echo "$current"
                return 0
            fi
        fi
        
        local parent="$(dirname "$current")"
        if [ "$parent" = "$current" ]; then
            break
        fi
        current="$parent"
        depth=$((depth + 1))
    done
    
    echo "ERROR: Could not find project root" >&2
    return 1
}

PROJECT_ROOT=$(find_project_root)
if [ -z "$PROJECT_ROOT" ]; then
    exit 1
fi

echo "Project root: $PROJECT_ROOT"

BUILD_DIR="$PROJECT_ROOT/cmake-build-debug"
BIN_DIR="$BUILD_DIR/bin"
UNREAL_QT="$BIN_DIR/unreal-qt"

# Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    echo "Creating debug build directory..."
    mkdir -p "$BUILD_DIR"
fi

# Configure CMake for debug build
cd "$BUILD_DIR"
if [ ! -f "CMakeCache.txt" ]; then
    echo "Configuring CMake for Debug build..."
    cmake -DCMAKE_BUILD_TYPE=Debug "$PROJECT_ROOT"
fi

# Build unreal-qt target
echo "Building unreal-qt (Debug)..."
cmake --build . --target unreal-qt -j8

# Verify binary exists and is fresh
if [ ! -f "$UNREAL_QT" ]; then
    echo "ERROR: unreal-qt binary not found at $UNREAL_QT"
    exit 1
fi

echo "Binary built: $UNREAL_QT"
ls -lh "$UNREAL_QT"

# Kill any existing unreal-qt instances
echo "Stopping any existing unreal-qt instances..."
pkill -9 unreal-qt 2>/dev/null || true
sleep 2

# Start unreal-qt with WebAPI
LOG_FILE="/tmp/unreal-qt-debug.log"
echo "Starting unreal-qt with WebAPI..."
echo "Log file: $LOG_FILE"

"$UNREAL_QT" --webapi --webapi-port 8090 > "$LOG_FILE" 2>&1 &
UNREAL_PID=$!

echo "Started unreal-qt with PID: $UNREAL_PID"

# Wait for services to be ready
echo "Waiting for services to start..."
sleep 5

# Check CLI port (8765)
echo -n "Checking CLI port 8765... "
if nc -z localhost 8765 2>/dev/null; then
    echo "✓ Ready"
else
    echo "✗ Not available"
    echo "WARNING: CLI port 8765 not responding"
fi

# Check WebAPI port (8090)
echo -n "Checking WebAPI port 8090... "
if curl -s http://localhost:8090/api/v1/emulator > /dev/null 2>&1; then
    echo "✓ Ready"
else
    echo "✗ Not available"
    echo "ERROR: WebAPI port 8090 not responding"
    echo "Check log file: $LOG_FILE"
    tail -20 "$LOG_FILE"
    exit 1
fi

echo ""
echo "============================================"
echo "✓ unreal-qt is running and ready"
echo "  PID: $UNREAL_PID"
echo "  CLI: telnet localhost 8765"
echo "  WebAPI: http://localhost:8090"
echo "  Log: $LOG_FILE"
echo "============================================"
echo ""
echo "To stop: kill $UNREAL_PID"
echo "To view logs: tail -f $LOG_FILE"
