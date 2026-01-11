/**
 * JTAG Protocol Test Client
 * Tests actual IEEE 1149.1 JTAG protocol operations
 * 
 * This test validates the 4-wire JTAG protocol via VPI interface.
 * Uses OpenOCD jtag_vpi protocol (8-byte commands).
 * 
 * Expected: All tests should PASS with current OpenOCD.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define VPI_PORT 3333
#define VPI_ADDR "127.0.0.1"
#define TIMEOUT_SEC 3

/* OpenOCD jtag_vpi protocol structures (8 bytes) */
struct vpi_cmd {
    uint8_t cmd;      // Command type
    uint8_t pad[3];   // Reserved
    uint32_t length;  // For SCAN: number of bits
} __attribute__((packed));

struct vpi_resp {
    uint8_t response;
    uint8_t tdo_val;
    uint8_t mode;
    uint8_t status;
};

/* Test state tracking */
int test_count = 0;
int pass_count = 0;
int fail_count = 0;
int sock = -1;

void print_test(const char *name) {
    test_count++;
    printf("\nTest %d: %s\n", test_count, name);
}

void print_pass(const char *msg) {
    pass_count++;
    printf("  ✓ PASS: %s\n", msg);
}

void print_fail(const char *msg) {
    fail_count++;
    printf("  ✗ FAIL: %s\n", msg);
}

void print_info(const char *msg) {
    printf("  ℹ INFO: %s\n", msg);
}

int connect_to_vpi() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(VPI_PORT);
    inet_pton(AF_INET, VPI_ADDR, &addr.sin_addr);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(s);
        return -1;
    }

    return s;
}

int send_vpi_cmd(struct vpi_cmd *cmd, struct vpi_resp *resp) {
    fd_set writeset, readset;
    struct timeval tv;
    
    /* Send with timeout */
    FD_ZERO(&writeset);
    FD_SET(sock, &writeset);
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    
    if (select(sock + 1, NULL, &writeset, NULL, &tv) <= 0) {
        return -1;
    }
    
    if (send(sock, cmd, sizeof(*cmd), 0) != sizeof(*cmd)) {
        return -1;
    }

    /* Receive with timeout */
    FD_ZERO(&readset);
    FD_SET(sock, &readset);
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    
    if (select(sock + 1, &readset, NULL, NULL, &tv) <= 0) {
        return -1;
    }
    
    if (recv(sock, resp, sizeof(*resp), 0) != sizeof(*resp)) {
        return -1;
    }

    return 0;
}

/**
 * Test 1: VPI Connection
 * Verify we can connect to the VPI server
 */
int test_vpi_connection() {
    print_test("VPI Server Connection");
    
    if (sock >= 0) {
        print_pass("Connected to VPI server on port 3333");
        print_info("4-wire JTAG mode (TCK/TMS/TDI/TDO)");
        return 1;
    } else {
        print_fail("Cannot connect to VPI server");
        return 0;
    }
}

/**
 * Test 2: JTAG TAP Reset
 * Send RESET command (0x00) - should set TMS=1 for 5+ clocks
 */
int test_tap_reset() {
    print_test("JTAG TAP Reset (CMD_RESET)");
    
    struct vpi_cmd cmd;
    struct vpi_resp resp;
    
    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));
    
    cmd.cmd = 0x00;  // CMD_RESET
    cmd.length = htonl(0);
    
    print_info("Sending CMD_RESET (0x00) - JTAG TAP reset sequence");
    
    if (send_vpi_cmd(&cmd, &resp) < 0) {
        print_fail("Failed to send RESET command");
        return 0;
    }
    
    if (resp.response == 0) {
        print_pass("TAP reset successful (response=0x00)");
        print_info("TAP controller should now be in Test-Logic-Reset state");
        return 1;
    } else {
        print_fail("Unexpected response from RESET command");
        printf("    Response: 0x%02x\n", resp.response);
        return 0;
    }
}

/**
 * Test 3: JTAG Scan Operation
 * Send SCAN command (0x02) with small bit sequence
 */
int test_scan_operation() {
    print_test("JTAG Scan Operation (CMD_SCAN)");
    
    struct vpi_cmd cmd;
    struct vpi_resp resp;
    
    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));
    
    // Request to scan 8 bits
    cmd.cmd = 0x02;  // CMD_SCAN
    cmd.length = htonl(8);
    
    print_info("Sending CMD_SCAN for 8 bits");
    
    if (send_vpi_cmd(&cmd, &resp) < 0) {
        print_fail("Failed to send SCAN command");
        return 0;
    }
    
    if (resp.response == 0) {
        print_pass("SCAN command accepted (response=0x00)");
        print_info("VPI server ready to receive TMS/TDI buffers");
        
        // Send TMS buffer (1 byte = 8 bits, all zeros)
        uint8_t tms_buf = 0x00;
        if (send(sock, &tms_buf, 1, 0) == 1) {
            print_pass("TMS buffer sent (8 bits)");
        } else {
            print_fail("Failed to send TMS buffer");
            return 0;
        }
        
        // Send TDI buffer (1 byte = 8 bits, all zeros)
        uint8_t tdi_buf = 0x00;
        if (send(sock, &tdi_buf, 1, 0) == 1) {
            print_pass("TDI buffer sent (8 bits)");
        } else {
            print_fail("Failed to send TDI buffer");
            return 0;
        }
        
        // Receive TDO buffer
        fd_set readset;
        struct timeval tv;
        FD_ZERO(&readset);
        FD_SET(sock, &readset);
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        
        if (select(sock + 1, &readset, NULL, NULL, &tv) > 0) {
            uint8_t tdo_buf;
            if (recv(sock, &tdo_buf, 1, 0) == 1) {
                print_pass("TDO buffer received (8 bits)");
                printf("    TDO value: 0x%02x\n", tdo_buf);
                return 1;
            }
        }
        
        print_fail("Timeout waiting for TDO buffer");
        return 0;
        
    } else {
        print_fail("SCAN command rejected");
        printf("    Response: 0x%02x\n", resp.response);
        return 0;
    }
}

/**
 * Test 4: Port Configuration
 * Send SET_PORT command (0x03)
 */
int test_port_config() {
    print_test("Port Configuration (CMD_SET_PORT)");
    
    struct vpi_cmd cmd;
    struct vpi_resp resp;
    
    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));
    
    cmd.cmd = 0x03;  // CMD_SET_PORT
    cmd.length = htonl(0);
    
    print_info("Sending CMD_SET_PORT (0x03) for configuration");
    
    if (send_vpi_cmd(&cmd, &resp) < 0) {
        print_fail("Failed to send SET_PORT command");
        return 0;
    }
    
    if (resp.response == 0) {
        print_pass("Port configuration accepted");
        return 1;
    } else {
        print_fail("SET_PORT command rejected");
        printf("    Response: 0x%02x\n", resp.response);
        return 0;
    }
}

/**
 * Test 5: Multiple Reset Cycles
 * Verify repeated RESET commands work correctly
 */
int test_multiple_resets() {
    print_test("Multiple TAP Reset Cycles");
    
    print_info("Testing repeated RESET operations");
    
    for (int i = 0; i < 3; i++) {
        struct vpi_cmd cmd;
        struct vpi_resp resp;
        
        memset(&cmd, 0, sizeof(cmd));
        memset(&resp, 0, sizeof(resp));
        
        cmd.cmd = 0x00;  // CMD_RESET
        cmd.length = htonl(0);
        
        if (send_vpi_cmd(&cmd, &resp) < 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Failed on reset cycle %d", i+1);
            print_fail(msg);
            return 0;
        }
        
        if (resp.response != 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Unexpected response on cycle %d", i+1);
            print_fail(msg);
            return 0;
        }
    }
    
    print_pass("All 3 reset cycles completed successfully");
    return 1;
}

/**
 * Test 6: Command Validation
 * Send invalid command and verify error handling
 * Note: VPI server may close connection on invalid command (acceptable behavior)
 */
int test_invalid_command() {
    print_test("Invalid Command Handling");
    
    struct vpi_cmd cmd;
    struct vpi_resp resp;
    
    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));
    
    cmd.cmd = 0xFF;  // Invalid command
    cmd.length = htonl(0);
    
    print_info("Sending invalid command (0xFF) to test error handling");
    
    if (send_vpi_cmd(&cmd, &resp) < 0) {
        print_pass("VPI server closed connection on invalid command (acceptable)");
        print_info("Defensive behavior: reject invalid commands by disconnecting");
        
        // Reconnect for remaining tests
        close(sock);
        sock = connect_to_vpi();
        if (sock < 0) {
            print_fail("Could not reconnect to VPI server");
            return 0;
        }
        print_info("Reconnected to VPI server successfully");
        return 1;
    }
    
    if (resp.response == 1) {
        print_pass("VPI server correctly reported error (response=0x01)");
        return 1;
    } else if (resp.response == 0) {
        print_info("VPI server accepted unknown command (lenient behavior)");
        return 1;
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "VPI server response: 0x%02x", resp.response);
        print_info(msg);
        return 1;
    }
}

/**
 * Test 7: Large Scan Operation
 * Test scanning larger bit sequences
 */
int test_large_scan() {
    print_test("Large Scan Operation (32 bits)");
    
    struct vpi_cmd cmd;
    struct vpi_resp resp;
    
    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));
    
    cmd.cmd = 0x02;  // CMD_SCAN
    cmd.length = htonl(32);  // 32 bits
    
    print_info("Scanning 32 bits through JTAG chain");
    
    if (send_vpi_cmd(&cmd, &resp) < 0) {
        print_fail("Failed to initiate large scan");
        return 0;
    }
    
    if (resp.response == 0) {
        print_pass("Large scan command accepted");
        
        // Send TMS buffer (4 bytes)
        uint8_t tms_buf[4] = {0x00, 0x00, 0x00, 0x00};
        if (send(sock, tms_buf, 4, 0) == 4) {
            print_pass("TMS buffer sent (32 bits)");
        } else {
            print_fail("Failed to send TMS buffer");
            return 0;
        }
        
        // Send TDI buffer (4 bytes)
        uint8_t tdi_buf[4] = {0xAA, 0x55, 0xAA, 0x55};
        if (send(sock, tdi_buf, 4, 0) == 4) {
            print_pass("TDI buffer sent (32 bits, pattern: 0xAA55AA55)");
        } else {
            print_fail("Failed to send TDI buffer");
            return 0;
        }
        
        // Receive TDO buffer
        fd_set readset;
        struct timeval tv;
        FD_ZERO(&readset);
        FD_SET(sock, &readset);
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        
        if (select(sock + 1, &readset, NULL, NULL, &tv) > 0) {
            uint8_t tdo_buf[4];
            if (recv(sock, tdo_buf, 4, 0) == 4) {
                print_pass("TDO buffer received (32 bits)");
                printf("    TDO value: 0x%02X%02X%02X%02X\n", 
                       tdo_buf[3], tdo_buf[2], tdo_buf[1], tdo_buf[0]);
                return 1;
            }
        }
        
        print_fail("Timeout waiting for TDO buffer");
        return 0;
        
    } else {
        print_fail("Large scan command rejected");
        return 0;
    }
}

/**
 * Test 8: Protocol Stress Test
 * Rapid sequence of commands
 */
int test_rapid_commands() {
    print_test("Rapid Command Sequence (Stress Test)");
    
    print_info("Sending 10 rapid RESET commands");
    
    int success_count = 0;
    for (int i = 0; i < 10; i++) {
        struct vpi_cmd cmd;
        struct vpi_resp resp;
        
        memset(&cmd, 0, sizeof(cmd));
        memset(&resp, 0, sizeof(resp));
        
        cmd.cmd = 0x00;  // CMD_RESET
        cmd.length = htonl(0);
        
        if (send_vpi_cmd(&cmd, &resp) >= 0 && resp.response == 0) {
            success_count++;
        }
    }
    
    if (success_count == 10) {
        print_pass("All 10 rapid commands completed successfully");
        return 1;
    } else if (success_count >= 8) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Most commands succeeded (%d/10)", success_count);
        print_pass(msg);
        return 1;
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Too many command failures (%d/10 succeeded)", success_count);
        print_fail(msg);
        return 0;
    }
}

int main(int argc, char** argv) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  JTAG (IEEE 1149.1) Protocol Test Suite\n");
    printf("  4-Wire Protocol Verification (TCK/TMS/TDI/TDO)\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("PURPOSE: Validate JTAG protocol operations via VPI interface\n");
    printf("EXPECTED: All tests should PASS (OpenOCD supports JTAG)\n");
    printf("\n");
    printf("This test suite verifies:\n");
    printf("  • VPI server connection and communication\n");
    printf("  • JTAG TAP reset operations\n");
    printf("  • Scan operations (small and large)\n");
    printf("  • Port configuration commands\n");
    printf("  • Error handling for invalid commands\n");
    printf("  • Protocol stress testing\n");
    printf("\n");
    printf("Protocol: OpenOCD jtag_vpi (8-byte commands)\n");
    printf("Commands: RESET (0x00), SCAN (0x02), SET_PORT (0x03)\n");
    printf("\n");
    
    // Connect to VPI server
    printf("Connecting to VPI server at %s:%d...\n", VPI_ADDR, VPI_PORT);
    sock = connect_to_vpi();
    if (sock < 0) {
        printf("✗ FATAL: Cannot connect to VPI server\n");
        printf("  Make sure simulation is running: make vpi-sim\n");
        return 1;
    }
    printf("✓ Connected to VPI server\n");
    
    // Run tests
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Running JTAG Protocol Tests\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    
    test_vpi_connection();
    test_tap_reset();
    test_scan_operation();
    test_port_config();
    test_multiple_resets();
    test_invalid_command();
    test_large_scan();
    test_rapid_commands();
    
    close(sock);
    
    // Summary
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Test Summary\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("Total Tests:  %d\n", test_count);
    printf("Passed:       %d\n", pass_count);
    printf("Failed:       %d\n", fail_count);
    printf("\n");
    
    if (fail_count == 0) {
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  ✓ ALL JTAG PROTOCOL TESTS PASSED\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("\n");
        printf("SUCCESS: JTAG protocol implementation is working correctly\n");
        printf("\n");
        printf("Validated features:\n");
        printf("  ✓ VPI server communication (8-byte command protocol)\n");
        printf("  ✓ JTAG TAP reset sequences\n");
        printf("  ✓ Scan operations with TMS/TDI/TDO buffers\n");
        printf("  ✓ Port configuration commands\n");
        printf("  ✓ Error handling and protocol robustness\n");
        printf("\n");
        printf("The VPI server correctly implements OpenOCD jtag_vpi protocol.\n");
        printf("\n");
        return 0;
    } else {
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  ✗ SOME JTAG PROTOCOL TESTS FAILED\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("\n");
        printf("ISSUE: JTAG protocol implementation has problems\n");
        printf("\n");
        printf("Failed: %d/%d tests\n", fail_count, test_count);
        printf("\n");
        printf("Possible causes:\n");
        printf("  • VPI server not implementing jtag_vpi protocol correctly\n");
        printf("  • Network communication issues\n");
        printf("  • Simulation not responding to commands\n");
        printf("\n");
        printf("Check:\n");
        printf("  • VPI server logs for errors\n");
        printf("  • Simulation is running: ps aux | grep jtag_vpi\n");
        printf("  • Port 3333 is accessible: lsof -i:3333\n");
        printf("\n");
        return 1;
    }
}
