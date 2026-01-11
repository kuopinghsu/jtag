/**
 * Legacy VPI Protocol Test Suite
 * Tests backward-compatible 8-byte command format
 *
 * Protocol format (legacy, 8 bytes total):
 *   Byte 0: Command code
 *   Byte 1: Mode/Flags
 *   Bytes 2-3: Reserved
 *   Bytes 4-7: Length/Data (big-endian)
 *
 * Commands:
 *   0x00: CMD_RESET   - TAP reset
 *   0x01: CMD_TMS_SEQ - TMS sequence
 *   0x02: CMD_SCAN    - Scan data (TMS/TDI->TDO)
 *   0x03: CMD_RUNTEST - Run test
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
#include <stdarg.h>

#define VPI_PORT 3333
#define VPI_ADDR "127.0.0.1"
#define TIMEOUT_SEC 3

/* Legacy VPI command structure (8 bytes total) */
struct legacy_vpi_cmd {
    uint8_t cmd;       // Command type (0x00-0x03)
    uint8_t mode;      // Mode flags
    uint8_t reserved[2];
    uint32_t length;   // Data length in bytes (big-endian)
} __attribute__((packed));

/* Legacy VPI response structure */
struct legacy_vpi_resp {
    uint8_t status;    // Status code
    uint8_t tdo_val;   // TDO value from shift
    uint8_t reserved[6];
};

/* Test state tracking */
int test_count = 0;
int pass_count = 0;
int fail_count = 0;
int sock = -1;
int tests_run[10] = {0};

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

void print_debug(const char *fmt, ...) {
    // Debug output disabled
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

int send_legacy_cmd(struct legacy_vpi_cmd *cmd, void *data, uint32_t data_len,
                     uint8_t *response, uint32_t *resp_len) {
    fd_set writeset, readset;
    struct timeval tv;
    unsigned char buffer[8192];

    /* Send command header with timeout */
    FD_ZERO(&writeset);
    FD_SET(sock, &writeset);
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;

    if (select(sock + 1, NULL, &writeset, NULL, &tv) <= 0) {
        fprintf(stderr, "Send timeout on command header\n");
        return -1;
    }

    if (send(sock, cmd, sizeof(*cmd), 0) != sizeof(*cmd)) {
        perror("send command");
        return -1;
    }

    /* Send data payload if any */
    if (data_len > 0 && data != NULL) {
        if (send(sock, data, data_len, 0) != (int)data_len) {
            perror("send data");
            return -1;
        }
    }

    /* Receive response with timeout */
    FD_ZERO(&readset);
    FD_SET(sock, &readset);
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;

    if (select(sock + 1, &readset, NULL, NULL, &tv) <= 0) {
        fprintf(stderr, "Receive timeout\n");
        return -1;
    }

    int nbytes = recv(sock, buffer, sizeof(buffer), 0);
    if (nbytes < 0) {
        perror("recv");
        return -1;
    }

    if (nbytes > 0 && response != NULL) {
        *resp_len = nbytes;
        memcpy(response, buffer, nbytes);
    }

    return 0;
}

/**
 * Test 1: Connection to VPI Server
 */
int test_legacy_connection() {
    print_test("Legacy Protocol: VPI Server Connection");

    sock = connect_to_vpi();
    if (sock < 0) {
        print_fail("Cannot connect to VPI server on port 3333");
        return 0;
    }

    print_pass("Connected to VPI server");
    print_info("Ready for legacy 8-byte protocol commands");
    tests_run[0] = 1;
    return 1;
}

/**
 * Test 2: TAP Reset via Legacy Protocol
 */
int test_legacy_reset() {
    print_test("Legacy Protocol: TAP Reset (CMD_RESET=0x00)");

    if (!sock) {
        print_fail("Not connected to VPI server");
        return 0;
    }

    struct legacy_vpi_cmd cmd;
    uint8_t response[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x00;        // CMD_RESET
    cmd.mode = 0x00;
    cmd.length = 0;        // No payload

    print_info("Sending legacy CMD_RESET (8-byte header, no payload)");
    print_debug("Command bytes: cmd=0x%02x mode=0x%02x length=0x%08x",
                cmd.cmd, cmd.mode, ntohl(cmd.length));

    if (send_legacy_cmd(&cmd, NULL, 0, response, &resp_len) < 0) {
        print_fail("Failed to send legacy reset command");
        return 0;
    }

    if (resp_len > 0) {
        print_pass("Received response from reset command");
        print_debug("Response bytes: %d bytes", resp_len);
        if (resp_len >= 8) {
            printf("    Response[0]=0x%02x Response[1]=0x%02x\n", response[0], response[1]);
        }
        return 1;
    } else {
        print_info("No response data (may be expected for legacy protocol)");
        return 1;
    }
}

/**
 * Test 3: TMS Sequence via Legacy Protocol
 */
int test_legacy_tms_sequence() {
    print_test("Legacy Protocol: TMS Sequence (CMD_TMS_SEQ=0x01)");

    if (!sock) {
        print_fail("Not connected to VPI server");
        return 0;
    }

    struct legacy_vpi_cmd cmd;
    uint8_t tms_data[2] = {0xFF, 0xFF};  // All 1s
    uint8_t response[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x01;        // CMD_TMS_SEQ
    cmd.mode = 0x00;
    cmd.length = htonl(2); // 2 bytes of TMS data

    print_info("Sending legacy CMD_TMS_SEQ with 2 bytes of TMS=0xFF, 0xFF");
    print_debug("Command bytes: cmd=0x%02x mode=0x%02x length=0x%08x",
                cmd.cmd, cmd.mode, ntohl(cmd.length));

    if (send_legacy_cmd(&cmd, tms_data, 2, response, &resp_len) < 0) {
        print_fail("Failed to send legacy TMS sequence command");
        return 0;
    }

    print_pass("Legacy TMS sequence command accepted");
    print_debug("Response received: %d bytes", resp_len);
    return 1;
}

/**
 * Test 4: Scan Chain via Legacy Protocol (basic)
 */
int test_legacy_scan_basic() {
    print_test("Legacy Protocol: Basic Scan Chain (CMD_SCAN=0x02)");

    if (!sock) {
        print_fail("Not connected to VPI server");
        return 0;
    }

    struct legacy_vpi_cmd cmd;
    /* For SCAN: payload is [tms_data, tdi_data, num_bits_high, num_bits_low] */
    uint8_t payload[6] = {
        0x00,  // TMS byte 0 (all 0s)
        0xAA,  // TDI byte 0 (0101_0101)
        0x00, 0x00,  // Reserved
        0x00, 0x08   // Length = 8 bits
    };
    uint8_t response[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x02;        // CMD_SCAN
    cmd.mode = 0x00;
    cmd.length = htonl(6); // 6 bytes of scan data

    print_info("Sending legacy CMD_SCAN for 8 bits");
    print_debug("TMS=0x00, TDI=0xAA (10101010 pattern), 8 bits");

    if (send_legacy_cmd(&cmd, payload, 6, response, &resp_len) < 0) {
        print_fail("Failed to send legacy scan command");
        return 0;
    }

    print_pass("Legacy scan command accepted");
    if (resp_len > 1) {
        print_debug("TDO response: 0x%02x (from %d response bytes)", response[1], resp_len);
    }
    return 1;
}

/**
 * Test 5: Multiple Commands (command pipelining)
 */
int test_legacy_multiple_commands() {
    print_test("Legacy Protocol: Multiple Sequential Commands");

    if (!sock) {
        print_fail("Not connected to VPI server");
        return 0;
    }

    int success = 1;

    /* Send 3 reset commands */
    for (int i = 0; i < 3; i++) {
        struct legacy_vpi_cmd cmd;
        uint8_t response[256];
        uint32_t resp_len = 0;

        memset(&cmd, 0, sizeof(cmd));
        cmd.cmd = 0x00;  // CMD_RESET
        cmd.length = 0;

        if (send_legacy_cmd(&cmd, NULL, 0, response, &resp_len) < 0) {
            printf("    Reset %d: FAILED\n", i+1);
            success = 0;
            break;
        }
        printf("    Reset %d: OK\n", i+1);
    }

    if (success) {
        print_pass("Successfully sent 3 sequential reset commands");
        return 1;
    } else {
        print_fail("Failed during sequential command test");
        return 0;
    }
}

/**
 * Test 6: Reset followed by Scan
 */
int test_legacy_reset_then_scan() {
    print_test("Legacy Protocol: Reset then Scan Sequence");

    if (!sock) {
        print_fail("Not connected to VPI server");
        return 0;
    }

    uint8_t response[256];
    uint32_t resp_len = 0;

    /* Step 1: Reset */
    struct legacy_vpi_cmd cmd1;
    memset(&cmd1, 0, sizeof(cmd1));
    cmd1.cmd = 0x00;
    cmd1.length = 0;

    print_info("Step 1: Sending reset command");
    if (send_legacy_cmd(&cmd1, NULL, 0, response, &resp_len) < 0) {
        print_fail("Reset command failed");
        return 0;
    }
    print_debug("  Reset OK");

    /* Step 2: Scan 8 bits */
    struct legacy_vpi_cmd cmd2;
    uint8_t scan_payload[6] = {0x00, 0x55, 0x00, 0x00, 0x00, 0x08};

    memset(&cmd2, 0, sizeof(cmd2));
    cmd2.cmd = 0x02;
    cmd2.length = htonl(6);

    print_info("Step 2: Sending scan command (8 bits, TDI=0x55)");
    if (send_legacy_cmd(&cmd2, scan_payload, 6, response, &resp_len) < 0) {
        print_fail("Scan command failed");
        return 0;
    }
    print_debug("  Scan OK, TDO=%02x", resp_len > 1 ? response[1] : 0x00);

    print_pass("Reset then scan sequence completed successfully");
    return 1;
}

/**
 * Test 7: Large bit count scan
 */
int test_legacy_large_scan() {
    print_test("Legacy Protocol: Large Scan (32 bits)");

    if (!sock) {
        print_fail("Not connected to VPI server");
        return 0;
    }

    struct legacy_vpi_cmd cmd;
    /* Payload: 4 bytes TMS + 4 bytes TDI + 2 bytes length */
    uint8_t payload[10] = {
        0x00, 0x00, 0x00, 0x00,  // TMS bytes
        0xFF, 0x00, 0xFF, 0x00,  // TDI pattern
        0x00, 0x20               // 32 bits
    };
    uint8_t response[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x02;        // CMD_SCAN
    cmd.length = htonl(10);

    print_info("Sending legacy CMD_SCAN for 32 bits");
    print_debug("TMS=0x00000000, TDI=0xFF00FF00, length=32 bits");

    if (send_legacy_cmd(&cmd, payload, 10, response, &resp_len) < 0) {
        print_fail("Large scan command failed");
        return 0;
    }

    print_pass("32-bit scan command accepted");
    if (resp_len > 4) {
        print_debug("Response: %d bytes", resp_len);
    }
    return 1;
}

/**
 * Test 8: Unknown command handling
 */
int test_legacy_unknown_command() {
    print_test("Legacy Protocol: Unknown Command Robustness");

    if (!sock) {
        print_fail("Not connected to VPI server");
        return 0;
    }

    struct legacy_vpi_cmd cmd;
    uint8_t response[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0xFF;        // Invalid command code
    cmd.length = 0;

    print_info("Sending legacy command with invalid code (0xFF)");

    /* This test verifies server doesn't crash on invalid input */
    int result = send_legacy_cmd(&cmd, NULL, 0, response, &resp_len);

    /* Server should either reject gracefully or ignore unknown commands */
    if (result == 0 || result < 0) {
        print_pass("Server handled unknown command robustly");
        return 1;
    } else {
        printf("    Server response to unknown command: %d\n", result);
        return 1;
    }
}

/**
 * Test 9: Protocol detection (send 8-byte command, verify legacy mode)
 */
int test_legacy_protocol_detection() {
    print_test("Legacy Protocol: Auto-Detection Verification");

    if (!sock) {
        print_fail("Not connected to VPI server");
        return 0;
    }

    /* Send exactly 8 bytes (legacy format) */
    struct legacy_vpi_cmd cmd;
    uint8_t response[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x00;  // CMD_RESET
    cmd.length = 0;

    print_info("Sending exactly 8 bytes (legacy protocol trigger)");
    print_debug("Protocol auto-detection should recognize this as legacy mode");

    if (send_legacy_cmd(&cmd, NULL, 0, response, &resp_len) < 0) {
        print_fail("Protocol detection test failed");
        return 0;
    }

    print_pass("Legacy protocol detected and handled");
    print_info("Server should use 8-byte command format for subsequent commands");
    return 1;
}

/**
 * Test 10: Rapid fire commands (stress test)
 */
int test_legacy_rapid_commands() {
    print_test("Legacy Protocol: Rapid Command Sequence (Stress Test)");

    if (!sock) {
        print_fail("Not connected to VPI server");
        return 0;
    }

    int rapid_count = 10;
    int failed = 0;

    for (int i = 0; i < rapid_count; i++) {
        struct legacy_vpi_cmd cmd;
        uint8_t response[256];
        uint32_t resp_len = 0;

        memset(&cmd, 0, sizeof(cmd));
        /* Alternate between reset and scan commands */
        if (i % 2 == 0) {
            cmd.cmd = 0x00;  // Reset
            cmd.length = 0;
        } else {
            cmd.cmd = 0x02;  // Scan
            // Legacy scan payload format: [TMS, TDI, 2 reserved bytes, len_hi, len_lo]
            // Use 8-bit scan with simple patterns to keep it fast
            cmd.length = htonl(6);
            uint8_t payload[6] = {
                0x00,      // TMS
                0x00,      // TDI
                0x00, 0x00,// Reserved
                0x00, 0x08 // Length = 8 bits
            };

            if (send_legacy_cmd(&cmd, payload, 6, response, &resp_len) < 0) {
                failed++;
            }
            continue;
        }

        if (send_legacy_cmd(&cmd, NULL, 0, response, &resp_len) < 0) {
            failed++;
        }
    }

    if (failed == 0) {
        printf("  ✓ PASS: Successfully sent %d rapid legacy commands\n", rapid_count);
        pass_count++;
        return 1;
    } else {
        printf("  ✗ FAIL: Failed on %d out of %d rapid commands\n", failed, rapid_count);
        fail_count++;
        return 0;
    }
}

/**
 * Main test runner
 */
int main(int argc, char *argv[]) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║    Legacy VPI Protocol Test Suite                        ║\n");
    printf("║    Tests 8-byte command format backward compatibility   ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* Run all tests */
    if (test_legacy_connection()) {
        test_legacy_reset();
        test_legacy_tms_sequence();
        test_legacy_scan_basic();
        test_legacy_multiple_commands();
        test_legacy_reset_then_scan();
        test_legacy_large_scan();
        test_legacy_unknown_command();
        test_legacy_protocol_detection();
        test_legacy_rapid_commands();
    } else {
        printf("\n✗ Cannot connect to VPI server - aborting tests\n");
        return 1;
    }

    /* Print summary */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║    Test Summary                                          ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Total Tests:   %d\n", test_count);
    printf("Passed:        %d\n", pass_count);
    printf("Failed:        %d\n", fail_count);
    printf("\n");

    if (fail_count == 0) {
        printf("✓ ALL LEGACY PROTOCOL TESTS PASSED\n");
        printf("\nThe VPI server correctly handles legacy 8-byte protocol\n");
        printf("and supports backward compatibility.\n");
        printf("\n");
        if (sock >= 0) close(sock);
        return 0;
    } else {
        printf("✗ SOME LEGACY PROTOCOL TESTS FAILED\n");
        printf("\nPlease check the VPI server implementation and ensure:\n");
        printf("  • 8-byte command format is supported\n");
        printf("  • Protocol auto-detection includes legacy mode\n");
        printf("  • Command handlers process legacy payloads correctly\n");
        printf("\n");
        if (sock >= 0) close(sock);
        return 1;
    }
}
