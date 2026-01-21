#!/bin/bash
# Helper script to run unit tests from the root directory
# Usage: ./run_tests.sh [build|flash|monitor|all]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$SCRIPT_DIR/test"

# Default action
ACTION="${1:-all}"

cd "$TEST_DIR"

case "$ACTION" in
    build)
        echo "Building unit tests..."
        idf.py build
        ;;
    flash)
        echo "Flashing unit tests..."
        idf.py -p "${2:-/dev/ttyUSB0}" flash
        ;;
    monitor)
        echo "Monitoring unit tests..."
        idf.py -p "${2:-/dev/ttyUSB0}" monitor
        ;;
    all)
        echo "Building, flashing, and monitoring unit tests..."
        idf.py build
        idf.py -p "${2:-/dev/ttyUSB0}" flash monitor
        ;;
    clean)
        echo "Cleaning unit test build..."
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
        exit 1
        ;;
esac
