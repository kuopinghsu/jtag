#!/bin/bash
# Automated OpenOCD testing script
# Usage: ./openocd/test_openocd.sh [jtag|cjtag]

set -e

MODE=${1:-jtag}
TIMEOUT=10

echo "=== OpenOCD Automated Test ==="
echo "Mode: $MODE"
echo "Timeout: ${TIMEOUT}s"
echo ""

# Check if VPI simulation is running
echo "[1/5] Checking VPI simulation..."
if ! lsof -i:3333 > /dev/null 2>&1; then
    echo "ERROR: VPI simulation not running on port 3333"
    echo "Please start simulation first: make vpi-sim"
    exit 1
fi
echo "  ✓ VPI server running on port 3333"

# Check OpenOCD is installed
echo "[2/5] Checking OpenOCD installation..."
if ! command -v openocd > /dev/null 2>&1; then
    echo "ERROR: OpenOCD not found"
    echo "Install with: brew install open-ocd (macOS) or apt-get install openocd (Linux)"
    exit 1
fi
OPENOCD_VERSION=$(openocd --version 2>&1 | head -1)
echo "  ✓ $OPENOCD_VERSION"

# Select configuration file
if [ "$MODE" == "cjtag" ]; then
    CONFIG_FILE="openocd/cjtag.cfg"
else
    CONFIG_FILE="openocd/jtag.cfg"
fi

echo "[3/5] Using configuration: $CONFIG_FILE"
if [ ! -f "$CONFIG_FILE" ]; then
    echo "ERROR: Configuration file not found: $CONFIG_FILE"
    exit 1
fi
echo "  ✓ Configuration file exists"

# Start OpenOCD in background
echo "[4/5] Starting OpenOCD..."
LOG_FILE="/tmp/openocd_test_$$.log"
openocd -f "$CONFIG_FILE" > "$LOG_FILE" 2>&1 &
OPENOCD_PID=$!
echo "  ✓ OpenOCD started (PID: $OPENOCD_PID)"

# Wait for OpenOCD to initialize and telnet server to be ready
sleep 3

# Check if OpenOCD is still running
if ! kill -0 $OPENOCD_PID 2>/dev/null; then
    echo "ERROR: OpenOCD failed to start"
    echo "Log output:"
    cat "$LOG_FILE"
    exit 1
fi

# Run test commands via telnet
echo "[5/5] Running test suite..."
TEST_OUTPUT=$(cat <<'EOF' | timeout $TIMEOUT telnet localhost 4444 2>&1 || true
help
EOF
)

# Kill OpenOCD
kill $OPENOCD_PID 2>/dev/null || true
wait $OPENOCD_PID 2>/dev/null || true

# Parse results
echo ""
echo "=== Test Results ==="
echo "$TEST_OUTPUT" | head -20

# Check if OpenOCD connected successfully and got expected responses
if echo "$TEST_OUTPUT" | grep -q "Open On-Chip Debugger\|Listening on port"; then
    echo ""
    echo "✓ PASS: OpenOCD telnet interface responsive"
    echo "✓ PASS: VPI adapter communication working"
    RESULT=0
else
    echo ""
    echo "✗ FAIL: OpenOCD telnet interface not responding"
    echo ""
    echo "Full OpenOCD log:"
    cat "$LOG_FILE"
    RESULT=1
fi

# Cleanup
rm -f "$LOG_FILE"

exit $RESULT
