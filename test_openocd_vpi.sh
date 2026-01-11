#!/bin/bash

# Test script: Full integration test for OpenOCD + VPI

set -e

echo "=== OpenOCD VPI Integration Test ==="
echo ""

# Start VPI server in background
echo "[1/2] Starting VPI server on port 3333..."
./build/jtag_vpi --timeout 30 2>&1 | tee /tmp/vpi_integration_test.log &
VPI_PID=$!
echo "      VPI Server PID: $VPI_PID"

# Give server time to initialize
sleep 2

# Check if VPI is listening
if ! lsof -i:3333 >/dev/null 2>&1; then
    echo "ERROR: VPI server failed to bind to port 3333"
    kill $VPI_PID 2>/dev/null || true
    exit 1
fi
echo "      ✓ VPI server listening on port 3333"

# Run OpenOCD
echo ""
echo "[2/2] Connecting OpenOCD..."
echo "      Attempting to read IDCODE..."
echo ""

OPENOCD_OUTPUT=$(timeout 12 openocd -f openocd/jtag.cfg 2>&1 || true)

echo "$OPENOCD_OUTPUT"
echo ""

# Kill VPI server
echo "Cleaning up..."
kill $VPI_PID 2>/dev/null || true
wait $VPI_PID 2>/dev/null || true

# Check for success indicators
echo ""
echo "=== Test Results ==="

if echo "$OPENOCD_OUTPUT" | grep -q "idcode"; then
    echo "✓ OpenOCD detected TAP with IDCODE"
    IDCODE=$(echo "$OPENOCD_OUTPUT" | grep -i "idcode" | head -1)
    echo "  Found: $IDCODE"
    
    if echo "$IDCODE" | grep -qi "0x1dead3ff\|1dead3ff\|DEAD3FF"; then
        echo "✓✓ CORRECT IDCODE: 0x1DEAD3FF"
        echo ""
        echo "=== TEST PASSED ==="
        exit 0
    else
        echo "✗ Wrong IDCODE value"
    fi
else
    echo "Check VPI log for details:"
    tail -100 /tmp/vpi_integration_test.log | grep -E "\[VPI\]" || echo "(no VPI messages found)"
fi

echo ""
echo "=== TEST FAILED ==="
exit 1
