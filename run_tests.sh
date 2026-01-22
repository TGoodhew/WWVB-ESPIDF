#!/bin/bash
# Helper script to run unit tests from the root directory
# Usage: ./run_tests.sh [build|flash|monitor|all|clean] [port]

# Don't exit on error for serial port operations (handled below)
set +e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$SCRIPT_DIR/test"

# Default action
ACTION="${1:-all}"
PORT="${2:-/dev/ttyUSB0}"

cd "$TEST_DIR"

# Function to check if serial port is accessible
check_serial_port() {
    local port="$1"
    
    # Check if port exists
    if [ ! -e "$port" ]; then
        echo "ERROR: Serial port $port does not exist."
        echo ""
        echo "Troubleshooting:"
        echo "  1. Check if ESP32 is connected via USB"
        echo "  2. List available ports:"
        echo "     - Linux: ls /dev/tty* | grep -E 'USB|ACM'"
        echo "     - macOS: ls /dev/cu.*"
        echo "     - Windows: Check Device Manager"
        echo "  3. Specify correct port: $0 $ACTION /dev/ttyUSB1"
        return 1
    fi
    
    # Check if port is locked (in use by another process)
    if ! timeout 1 bash -c "echo > $port" 2>/dev/null; then
        echo "WARNING: Serial port $port may be in use by another process."
        echo ""
        echo "Common causes:"
        echo "  1. VSCode/IDE serial monitor is open - CLOSE IT FIRST"
        echo "  2. Another idf.py monitor session is running"
        echo "  3. Screen/minicom/picocom is using the port"
        echo ""
        echo "Solutions:"
        echo "  1. Close any serial monitors or terminal programs using the port"
        echo "  2. In VSCode: Close the Serial Monitor panel"
        echo "  3. Kill processes: sudo killall screen minicom picocom"
        echo "  4. Find process using port: lsof $port (Linux/macOS)"
        echo ""
        echo "Continuing anyway (idf.py will provide more details)..."
        echo ""
    fi
    
    return 0
}

case "$ACTION" in
    build)
        echo "Building unit tests..."
        set -e
        idf.py build
        ;;
    flash)
        echo "Flashing unit tests to $PORT..."
        check_serial_port "$PORT"
        set -e
        idf.py -p "$PORT" flash
        ;;
    monitor)
        echo "Monitoring unit tests on $PORT..."
        check_serial_port "$PORT"
        echo "Press Ctrl+] to exit monitor"
        set -e
        idf.py -p "$PORT" monitor
        ;;
    all)
        echo "Building, flashing, and monitoring unit tests..."
        echo ""
        
        # Build first (always succeeds or fails cleanly)
        echo "Step 1/2: Building..."
        set -e
        idf.py build
        set +e
        
        echo ""
        echo "Step 2/2: Flashing and monitoring on $PORT..."
        check_serial_port "$PORT"
        
        # Flash and monitor
        set -e
        idf.py -p "$PORT" flash monitor
        ;;
    clean)
        echo "Cleaning unit test build..."
        set -e
        idf.py fullclean
        ;;
    *)
        echo "Usage: $0 [build|flash|monitor|all|clean] [port]"
        echo ""
        echo "Commands:"
        echo "  build   - Build the unit tests"
        echo "  flash   - Flash the unit tests to device"
        echo "  monitor - Monitor serial output"
        echo "  all     - Build, flash, and monitor (default)"
        echo "  clean   - Clean build artifacts"
        echo ""
        echo "Port: Serial port (default: /dev/ttyUSB0)"
        echo ""
        echo "Examples:"
        echo "  $0 build              # Build only"
        echo "  $0 all /dev/ttyUSB1   # Build, flash, monitor on different port"
        echo "  $0 flash /dev/ttyACM0 # Flash only to specific port"
        exit 1
        ;;
esac
