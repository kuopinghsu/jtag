# OpenOCD test script for JTAG/cJTAG verification
# Usage: Run OpenOCD with -f openocd/jtag.cfg or -f openocd/cjtag.cfg
#        Then in telnet session: source openocd/test.tcl

proc test_jtag {} {
    echo "=== JTAG Test Suite ==="
    
    # Test 1: Scan chain verification
    echo "\n[Test 1] Scan chain verification..."
    scan_chain
    
    # Test 2: TAP state check
    echo "\n[Test 2] Check TAP state..."
    jtag tapisenabled riscv.cpu
    if {[jtag tapisenabled riscv.cpu]} {
        echo "  PASS: TAP is enabled"
    } else {
        echo "  FAIL: TAP is not enabled"
    }
    
    # Test 3: IDCODE read
    echo "\n[Test 3] Read IDCODE..."
    set idcode [jtag cget riscv.cpu -idcode]
    echo "  IDCODE: $idcode"
    if {$idcode == 0x1dead3ff} {
        echo "  PASS: IDCODE matches expected value"
    } else {
        echo "  FAIL: IDCODE mismatch (expected: 0x1dead3ff)"
    }
    
    # Test 4: IR scan test
    echo "\n[Test 4] IR scan test..."
    irscan riscv.cpu 0x01
    echo "  PASS: IR scan completed"
    
    # Test 5: Target examination
    echo "\n[Test 5] Examine target..."
    riscv.cpu arp_examine
    echo "  PASS: Target examined"
    
    # Test 6: DMI access test
    echo "\n[Test 6] DMI register access..."
    echo "  Note: Requires proper DMI implementation in debug module"
    
    echo "\n=== Test Suite Complete ==="
}

proc test_cjtag {} {
    echo "=== cJTAG (OScan1) Test Suite ==="
    
    echo "\n[Info] cJTAG mode requires:"
    echo "  - Simulation started with mode_select=1"
    echo "  - VPI server properly handling OScan1 protocol"
    echo "  - 2-wire interface active (TMSC/TDI)"
    
    # Run same tests as JTAG mode
    test_jtag
    
    echo "\n[Info] Additional cJTAG-specific tests:"
    echo "  - OAC (Offline Access Controller) detection"
    echo "  - JScan packet handling"
    echo "  - Zero insertion/deletion"
    echo "  - Scanning Format 0 (SF0) protocol"
    echo "  Verify these in simulation waveforms"
}

proc quick_test {} {
    echo "=== Quick Connectivity Test ==="
    scan_chain
    set idcode [jtag cget riscv.cpu -idcode]
    echo "IDCODE: $idcode"
    if {$idcode == 0x1dead3ff} {
        echo "PASS: Device connected successfully"
    } else {
        echo "FAIL: Device not responding correctly"
    }
}

# Display available commands
echo "\n=== Available Test Commands ==="
echo "  test_jtag   - Run complete JTAG test suite"
echo "  test_cjtag  - Run cJTAG test suite"
echo "  quick_test  - Quick connectivity check"
echo "\nExample: test_jtag"
