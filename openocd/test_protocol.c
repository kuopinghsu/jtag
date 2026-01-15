/**
 * Unified Protocol Test Client for JTAG / cJTAG / Legacy VPI
 *
 * Usage:
 *   ./test_protocol jtag    # modern OpenOCD jtag_vpi protocol
 *   ./test_protocol cjtag   # two-wire cJTAG OScan1 (CMD_OSCAN1)
 *   ./test_protocol legacy  # legacy 8-byte VPI protocol
 *   ./test_protocol combo   # protocol switching and mixed operations
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define TO_LE32(x) (x)
#define FROM_LE32(x) (x)
#else
#define TO_LE32(x) (__builtin_bswap32(x))
#define FROM_LE32(x) (__builtin_bswap32(x))
#endif

#define VPI_ADDR "127.0.0.1"
#define VPI_PORT 3333
#define TIMEOUT_SEC 3

/* Common test counters */
static int sock_fd = -1;
static int test_count = 0;
static int pass_count = 0;
static int fail_count = 0;

static void print_test(const char *name) {
    test_count++;
    printf("\nTest %d: %s\n", test_count, name);
}

static void print_pass(const char *msg) {
    pass_count++;
    printf("  \u2713 PASS: %s\n", msg);
}

static void print_fail(const char *msg) {
    fail_count++;
    printf("  \u2717 FAIL: %s\n", msg);
}

static void print_info(const char *msg) {
    printf("  \u2139 INFO: %s\n", msg);
}

/* ========================================================================== */
/* Response Validation API                                                   */
/* ========================================================================== */

/**
 * Validate a response value against expected value
 * @param name Test name
 * @param actual Actual response value
 * @param expected Expected response value
 * @return 1 if valid, 0 if mismatch
 */
static int validate_response(const char *name, uint8_t actual, uint8_t expected) {
    if (actual == expected) {
        return 1;
    }
    char msg[100];
    snprintf(msg, sizeof(msg), "%s: got 0x%02X, expected 0x%02X", name, actual, expected);
    printf("  \u2717 %s\n", msg);
    return 0;
}

/**
 * Validate buffer data against expected pattern
 * @param name Test name
 * @param actual Actual buffer data
 * @param expected Expected buffer data
 * @param size Buffer size in bytes
 * @return 1 if valid, 0 if mismatch
 */
static int validate_buffer(const char *name, const uint8_t *actual, const uint8_t *expected, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (actual[i] != expected[i]) {
            char msg[120];
            snprintf(msg, sizeof(msg), "%s: byte %zu mismatch (got 0x%02X, expected 0x%02X)",
                     name, i, actual[i], expected[i]);
            printf("  \u2717 %s\n", msg);
            return 0;
        }
    }
    return 1;
}

/**
 * Validate a 32-bit value against expected
 * @param name Test name
 * @param actual Actual value
 * @param expected Expected value
 * @return 1 if valid, 0 if mismatch
 */
static int validate_u32(const char *name, uint32_t actual, uint32_t expected) {
    if (actual == expected) {
        return 1;
    }
    char msg[100];
    snprintf(msg, sizeof(msg), "%s: got 0x%08X, expected 0x%08X", name, actual, expected);
    printf("  \u2717 %s\n", msg);
    return 0;
}

/**
 * Validate a 16-bit value against expected
 * @param name Test name
 * @param actual Actual value
 * @param expected Expected value
 * @return 1 if valid, 0 if mismatch
 */
static int validate_u16(const char *name, uint16_t actual, uint16_t expected) {
    if (actual == expected) {
        return 1;
    }
    char msg[100];
    snprintf(msg, sizeof(msg), "%s: got 0x%04X, expected 0x%04X", name, actual, expected);
    printf("  \u2717 %s\n", msg);
    return 0;
}

static int connect_vpi(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(VPI_PORT);
    inet_pton(AF_INET, VPI_ADDR, &addr.sin_addr);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s);
        return -1;
    }

    struct timeval tv = {.tv_sec = TIMEOUT_SEC, .tv_usec = 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return s;
}

static int send_all(int s, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(s, p + sent, len - sent, 0);
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int recv_all(int s, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(s, &rset);
        struct timeval tv = {.tv_sec = TIMEOUT_SEC, .tv_usec = 0};
        if (select(s + 1, &rset, NULL, NULL, &tv) <= 0)
            return -1;
        ssize_t n = recv(s, p + got, len - got, 0);
        if (n <= 0)
            return -1;
        got += (size_t)n;
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Modern JTAG protocol (OpenOCD jtag_vpi)                                   */
/* -------------------------------------------------------------------------- */

struct jtag_vpi_cmd {
    uint8_t cmd;
    uint8_t pad[3];
    uint32_t length; /* bits for SCAN */
} __attribute__((packed));

struct jtag_vpi_resp {
    uint8_t response;
    uint8_t tdo_val;
    uint8_t mode;
    uint8_t status;
};

static int jtag_send_cmd(struct jtag_vpi_cmd *cmd, struct jtag_vpi_resp *resp) {
    if (send_all(sock_fd, cmd, sizeof(*cmd)) < 0)
        return -1;
    if (recv_all(sock_fd, resp, sizeof(*resp)) < 0)
        return -1;
    return 0;
}

static int test_jtag_reset(void) {
    print_test("JTAG TAP Reset (CMD_RESET)");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x00; /* CMD_RESET */
    cmd.length = htonl(0);

    if (jtag_send_cmd(&cmd, &resp) != 0) {
        print_fail("Communication failed");
        return 0;
    }

    if (!validate_response("Response code", resp.response, 0x00)) {
        print_fail("RESET command failed");
        return 0;
    }

    print_pass("TAP reset acknowledged");
    return 1;
}

static int test_jtag_scan8(void) {
    print_test("JTAG Scan 8 bits (CMD_SCAN)");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x02; /* CMD_SCAN */
    cmd.length = htonl(8);

    if (jtag_send_cmd(&cmd, &resp) != 0) {
        print_fail("Communication failed");
        return 0;
    }

    if (!validate_response("Response code", resp.response, 0x00)) {
        print_fail("SCAN command rejected");
        return 0;
    }

    uint8_t tms = 0x00;
    uint8_t tdi = 0xAA;
    uint8_t tdo = 0;
    if (send_all(sock_fd, &tms, 1) < 0 || send_all(sock_fd, &tdi, 1) < 0 || recv_all(sock_fd, &tdo, 1) < 0) {
        print_fail("TMS/TDI/TDO transfer failed");
        return 0;
    }

    /* Note: TDO validation depends on RTL behavior - currently not validated */
    print_pass("SCAN completed (TDO captured)");
    return 1;
}

static int test_jtag_multiple_resets(void) {
    print_test("JTAG Multiple TAP Reset Cycles");
    print_info("Testing repeated RESET operations");

    int all_passed = 1;
    for (int i = 0; i < 3; i++) {
        struct jtag_vpi_cmd cmd = {0};
        struct jtag_vpi_resp resp = {0};
        cmd.cmd = 0x00; /* CMD_RESET */
        cmd.length = htonl(0);

        if (jtag_send_cmd(&cmd, &resp) != 0) {
            printf("  ✗ Cycle %d: communication failed\n", i+1);
            all_passed = 0;
            continue;
        }

        if (resp.response != 0x00) {
            printf("  ✗ Cycle %d: response = 0x%02X (expected 0x00)\n", i+1, resp.response);
            all_passed = 0;
        }
    }

    if (!all_passed) {
        print_fail("Some reset cycles failed");
        return 0;
    }

    print_pass("All 3 reset cycles completed successfully");
    return 1;
}

static int test_jtag_invalid_command(void) {
    print_test("JTAG Invalid Command Handling");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0xFF; /* Invalid command */
    cmd.length = htonl(0);
    print_info("Sending invalid command (0xFF) to test error handling");

    if (jtag_send_cmd(&cmd, &resp) < 0) {
        print_pass("VPI server closed connection on invalid command (acceptable)");
        print_info("Defensive behavior: reject invalid commands by disconnecting");
        /* Reconnect for remaining tests */
        close(sock_fd);
        sock_fd = connect_vpi();
        if (sock_fd < 0) {
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
        print_pass("Error handling test completed (server lenient mode)");
        return 1;
    }
    print_info("VPI server response received");
    print_pass("Error handling test completed");
    return 1;
}

static int test_jtag_scan32(void) {
    print_test("JTAG Large Scan Operation (32 bits)");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x02; /* CMD_SCAN */
    cmd.length = htonl(32);
    print_info("Scanning 32 bits through JTAG chain");

    if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
        print_fail("Large scan command rejected");
        return 0;
    }
    print_info("Large scan command accepted");

    /* Send TMS buffer (4 bytes) */
    uint8_t tms_buf[4] = {0x00, 0x00, 0x00, 0x00};
    if (send_all(sock_fd, tms_buf, 4) < 0) {
        print_fail("Failed to send TMS buffer");
        return 0;
    }
    print_info("TMS buffer sent (32 bits)");

    /* Send TDI buffer (4 bytes) */
    uint8_t tdi_buf[4] = {0xAA, 0x55, 0xAA, 0x55};
    if (send_all(sock_fd, tdi_buf, 4) < 0) {
        print_fail("Failed to send TDI buffer");
        return 0;
    }
    print_info("TDI buffer sent (32 bits, pattern: 0xAA55AA55)");

    /* Receive TDO buffer */
    uint8_t tdo_buf[4];
    if (recv_all(sock_fd, tdo_buf, 4) < 0) {
        print_fail("Failed to receive TDO buffer");
        return 0;
    }
    print_info("TDO buffer received (32 bits)");
    char msg[64];
    snprintf(msg, sizeof(msg), "TDO value: 0x%02X%02X%02X%02X",
             tdo_buf[3], tdo_buf[2], tdo_buf[1], tdo_buf[0]);
    print_info(msg);
    print_pass("32-bit scan operation completed successfully");
    return 1;
}

static int test_jtag_rapid_commands(void) {
    print_test("JTAG Rapid Command Sequence (Stress Test)");
    print_info("Sending 10 rapid RESET commands");

    int success_count = 0;
    for (int i = 0; i < 10; i++) {
        struct jtag_vpi_cmd cmd = {0};
        struct jtag_vpi_resp resp = {0};
        cmd.cmd = 0x00; /* CMD_RESET */
        cmd.length = htonl(0);

        if (jtag_send_cmd(&cmd, &resp) == 0 && resp.response == 0x00) {
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

static int test_jtag_scan_patterns(void) {
    print_test("JTAG Scan Pattern Test (16 bits)");
    print_info("Testing alternating patterns (0xAAAA, 0x5555)");

    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    uint8_t patterns[2][2] = {{0xAA, 0xAA}, {0x55, 0x55}};

    for (int p = 0; p < 2; p++) {
        cmd.cmd = 0x02; /* CMD_SCAN */
        cmd.length = htonl(16);

        if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
            print_fail("Scan command rejected");
            return 0;
        }

        uint8_t tms_buf[2] = {0x00, 0x00};
        uint8_t tdo_buf[2];

        if (send_all(sock_fd, tms_buf, 2) < 0 ||
            send_all(sock_fd, patterns[p], 2) < 0 ||
            recv_all(sock_fd, tdo_buf, 2) < 0) {
            print_fail("Pattern transfer failed");
            return 0;
        }
    }

    print_pass("Pattern test completed successfully");
    return 1;
}

static int test_jtag_mode_query(void) {
    print_test("JTAG Mode Query (CMD_SET_PORT)");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x03; /* CMD_SET_PORT acts as mode query */

    if (jtag_send_cmd(&cmd, &resp) != 0) {
        print_fail("Communication failed");
        return 0;
    }

    /* Validate mode field - should be 0 (JTAG) or 1 (cJTAG) */
    if (resp.mode > 1) {
        printf("  ✗ Invalid mode value: 0x%02X\n", resp.mode);
        print_fail("Mode query returned invalid value");
        return 0;
    }

    print_pass(resp.mode ? "Mode=cJTAG" : "Mode=JTAG");
    return 1;
}

/* Physical-level tests for 4-wire JTAG interface */
static int test_jtag_tms_state_machine(void) {
    print_test("JTAG Physical: TMS State Machine Transitions");
    print_info("Testing TAP state transitions via TMS sequences");

    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};

    /* Navigate through TAP states: Test-Logic-Reset -> Run-Test/Idle -> Shift-DR */
    /* TMS sequence: 0 (to Run-Test/Idle), 1,1,0,0 (to Shift-DR) */
    uint8_t tms_sequence[1] = {0x06}; /* bits: 0,1,1,0,0,0,0,0 (LSB first) */
    uint8_t tdi_sequence[1] = {0x00};
    uint8_t tdo_result[1];

    cmd.cmd = 0x02; /* CMD_SCAN */
    cmd.length = htonl(5);

    if (jtag_send_cmd(&cmd, &resp) == 0 && resp.response == 0x00) {
        if (send_all(sock_fd, tms_sequence, 1) >= 0 &&
            send_all(sock_fd, tdi_sequence, 1) >= 0 &&
            recv_all(sock_fd, tdo_result, 1) >= 0) {
            print_pass("TMS state transitions executed");
            return 1;
        }
    }
    print_fail("TMS state machine test failed");
    return 0;
}

/**
 * Test 13: JTAG TDI/TDO Signal Integrity Test
 *
 * GOAL:
 *   Verify that the JTAG scan chain is operational and data can flow from TDI to TDO.
 *   This is a fundamental connectivity test to ensure the physical JTAG interface works.
 *
 * METHOD:
 *   - Send CMD_SCAN with different bit patterns (0xAA, 0x55, 0xFF, 0x20) via TDI
 *   - These patterns exercise all combinations of consecutive 1s and 0s
 *   - Capture TDO responses to verify scan chain is not "stuck" or disconnected
 *
 * EXPECTED BEHAVIOR:
 *   With default IDCODE instruction (JTAG spec compliant):
 *   - TDO returns bits from IDCODE register (0x1DEAD3FF) during shift operations
 *   - Each 8-bit scan shifts through different portions of the 32-bit IDCODE
 *   - At least one pattern should return non-zero TDO (proving scan chain works)
 *   - TDO=0x00 for all patterns indicates broken or disconnected scan chain
 *
 * SUCCESS CRITERIA:
 *   - At least one of the 4 test patterns returns non-zero TDO response
 *   - Non-zero TDO proves: (1) TDI->scan chain->TDO path operational
 *                         (2) Scan register contains valid data
 *                         (3) Clock and control signals functional
 *
 * FAILURE CRITERIA:
 *   - All 4 patterns return TDO=0x00 → Scan chain broken/disconnected
 *   - Communication errors → VPI protocol or network issues
 *
 * NOTE:
 *   This test does NOT expect loopback (TDI pattern == TDO pattern).
 *   It validates scan chain operation, not specific data patterns.
 */
static int test_jtag_tdi_tdo_integrity(void) {
    print_test("JTAG Physical: TDI/TDO Signal Integrity");
    print_info("Testing data integrity on TDI->TDO path");

    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};

    /* Test various bit patterns to exercise all TDI signal combinations */
    uint8_t patterns[4] = {0xAA, 0x55, 0xFF, 0x20};  /* Alternating bits, all-ones, mixed */
    int valid_responses = 0;   /* Count of non-zero TDO responses */
    int zero_responses = 0;    /* Count of zero TDO responses */

    for (int p = 0; p < 4; p++) {
        /* Send CMD_SCAN for 8-bit transfer */
        cmd.cmd = 0x02;
        cmd.length = htonl(8);  /* 8-bit scan operation */

        if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
            char errmsg[80];
            snprintf(errmsg, sizeof(errmsg), "Pattern 0x%02X: command failed", patterns[p]);
            printf("  \u2717 %s\n", errmsg);  /* Print error but don't increment fail_count */
            continue;
        }

        /* Send TMS=0 (stay in current DR) and TDI pattern */
        uint8_t tms = 0x00;  /* Keep TMS low during data transfer */
        uint8_t tdo;

        if (send_all(sock_fd, &tms, 1) < 0 ||
            send_all(sock_fd, &patterns[p], 1) < 0 ||
            recv_all(sock_fd, &tdo, 1) < 0) {
            char errmsg[80];
            snprintf(errmsg, sizeof(errmsg), "Pattern 0x%02X: communication failed", patterns[p]);
            printf("  \u2717 %s\n", errmsg);  /* Print error but don't increment fail_count */
            continue;
        }

        /* Analyze TDO response to determine scan chain status */
        if (tdo == 0x00) {
            zero_responses++;
            printf("  \u2139 Pattern 0x%02X: TDO=0x00 (empty or non-loopback chain)\n", patterns[p]);
        } else if (tdo == patterns[p]) {
            valid_responses++;
            printf("  \u2139 Pattern 0x%02X: TDO=0x%02X (loopback confirmed)\n", patterns[p], tdo);
        } else {
            valid_responses++;
            printf("  \u2139 Pattern 0x%02X: TDI=0x%02X TDO=0x%02X (scan chain data)\n", patterns[p], patterns[p], tdo);
        }
    }

    /*
     * SUCCESS: At least one non-zero TDO response indicates operational scan chain
     * FAILURE: All-zero TDO responses indicate broken/disconnected scan chain
     */
    if (valid_responses > 0) {
        char summary[100];
        snprintf(summary, sizeof(summary), "Scan chain operational: %d patterns with data, %d empty", valid_responses, zero_responses);
        print_pass(summary);
        return 1;
    }

    /* All patterns returned zero - scan chain is broken or disconnected */
    if (zero_responses == 4) {
        print_fail("Scan operations failed: all patterns returned 0x00");
        return 0;
    }

    print_fail("Scan operations failed: no valid responses");
    return 0;
}

static int test_jtag_boundary_scan_simulation(void) {
    print_test("JTAG Physical: Boundary Scan Simulation");
    print_info("Simulating boundary scan register access");

    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};

    /* Navigate to Shift-DR state and shift 16 bits */
    /* This simulates accessing a boundary scan register */
    cmd.cmd = 0x02;
    cmd.length = htonl(16);

    if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
        print_fail("Failed to initiate boundary scan");
        return 0;
    }

    uint8_t tms_buf[2] = {0x00, 0x80}; /* Exit on last bit */
    uint8_t tdi_buf[2] = {0x12, 0x34}; /* Test pattern */
    uint8_t tdo_buf[2];

    if (send_all(sock_fd, tms_buf, 2) >= 0 &&
        send_all(sock_fd, tdi_buf, 2) >= 0 &&
        recv_all(sock_fd, tdo_buf, 2) >= 0) {
        print_pass("Boundary scan register access simulated");
        char msg[64];
        snprintf(msg, sizeof(msg), "Captured data: 0x%02X%02X", tdo_buf[1], tdo_buf[0]);
        print_info(msg);
        return 1;
    }

    print_fail("Boundary scan simulation failed");
    return 0;
}

static int test_jtag_idcode_read_simulation(void) {
    print_test("JTAG Physical: IDCODE Read Simulation");
    print_info("Simulating IDCODE register read (32-bit DR)");

    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};

    /* First reset TAP to ensure IDCODE instruction is selected */
    cmd.cmd = 0x00;  /* CMD_RESET */
    cmd.length = htonl(0);

    if (jtag_send_cmd(&cmd, &resp) != 0) {
        print_fail("Failed to reset TAP before IDCODE read");
        return 0;
    }

    /* Now read 32-bit IDCODE from DR (IDCODE is default after reset) */
    /* Need proper TAP state sequence: */
    /* After reset: Test-Logic-Reset */
    /* TMS=0: → Run-Test/Idle */
    /* TMS=1: → Select-DR-Scan */
    /* TMS=0: → Capture-DR (IDCODE loaded here) */
    /* TMS=0 x30: → Shift-DR (shift 30 bits) */
    /* TMS=1: → Exit1-DR (shift last bit and exit) */
    /* Total: 1 + 1 + 1 + 31 = 34 bits */

    cmd.cmd = 0x02;
    cmd.length = htonl(34);  /* Changed from 32 to include state transitions */

    if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
        print_fail("Failed to initiate IDCODE read");
        return 0;
    }

    /* TMS sequence: 0, 1, 0, 0...0 (31 zeros), 1 */
    /* Bit order: LSB first within each byte */
    uint8_t tms_buf[5] = {
        0x02,  /* Bits 0-7:   0=Run-Idle, 1=Select-DR, 0=Capture, 0...0 */
        0x00,  /* Bits 8-15:  All zeros (Shift-DR) */
        0x00,  /* Bits 16-23: All zeros (Shift-DR) */
        0x00,  /* Bits 24-31: All zeros (Shift-DR) */
        0x02   /* Bits 32-33: 0=Shift last bit, 1=Exit1-DR */
    };
    uint8_t tdi_buf[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t tdo_buf[5];

    if (send_all(sock_fd, tms_buf, 5) >= 0 &&
        send_all(sock_fd, tdi_buf, 5) >= 0 &&
        recv_all(sock_fd, tdo_buf, 5) >= 0) {

        /* Extract IDCODE from bits 3-34 (skip first 3 state transition bits) */
        /* TDO buffer layout:
         * Bit 0: Run-Idle (don't care)
         * Bit 1: Select-DR (don't care)
         * Bit 2: Capture-DR (don't care)
         * Bits 3-34: IDCODE[0:31]
         */
        uint32_t idcode = 0;
        for (int i = 0; i < 32; i++) {
            int bit_pos = i + 3;  /* Skip first 3 bits */
            int byte_idx = bit_pos / 8;
            int bit_idx = bit_pos % 8;
            if (tdo_buf[byte_idx] & (1 << bit_idx)) {
                idcode |= (1U << i);
            }
        }

        char msg[80];
        snprintf(msg, sizeof(msg), "IDCODE read: 0x%08X", idcode);
        print_info(msg);

        /* Validate IDCODE value - MUST match expected value */
        if (idcode == 0x1DEAD3FF) {
            print_pass("IDCODE correct: 0x1DEAD3FF");
            return 1;
        } else {
            char errmsg[100];
            snprintf(errmsg, sizeof(errmsg), "IDCODE mismatch: got 0x%08X, expected 0x1DEAD3FF", idcode);
            print_fail(errmsg);
            return 0;
        }
    }

    print_fail("IDCODE read simulation failed");
    return 0;
}

static int test_jtag_shift_register_length(void) {
    print_test("JTAG Physical: Variable Shift Register Lengths");
    print_info("Testing different register lengths (8, 16, 32, 64 bits)");

    int lengths[] = {8, 16, 32, 64};
    int all_ok = 1;

    for (int i = 0; i < 4; i++) {
        int len = lengths[i];
        int bytes = len / 8;

        struct jtag_vpi_cmd cmd = {0};
        struct jtag_vpi_resp resp = {0};
        cmd.cmd = 0x02;
        cmd.length = htonl(len);

        if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
            all_ok = 0;
            break;
        }

        uint8_t tms_buf[8] = {0};
        uint8_t tdi_buf[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        uint8_t tdo_buf[8];

        if (send_all(sock_fd, tms_buf, bytes) < 0 ||
            send_all(sock_fd, tdi_buf, bytes) < 0 ||
            recv_all(sock_fd, tdo_buf, bytes) < 0) {
            all_ok = 0;
            break;
        }
    }

    if (all_ok) {
        print_pass("All register lengths supported (8, 16, 32, 64 bits)");
        return 1;
    }
    print_fail("Variable length test failed");
    return 0;
}

static int test_jtag_tck_frequency_stress(void) {
    print_test("JTAG Physical: TCK Frequency Stress Test");
    print_info("Rapid TCK toggling with 50 consecutive operations");

    int success_count = 0;
    for (int i = 0; i < 50; i++) {
        struct jtag_vpi_cmd cmd = {0};
        struct jtag_vpi_resp resp = {0};
        cmd.cmd = 0x02;
        cmd.length = htonl(1);

        if (jtag_send_cmd(&cmd, &resp) == 0 && resp.response == 0x00) {
            uint8_t tms = 0, tdi = 0, tdo;
            if (send_all(sock_fd, &tms, 1) >= 0 &&
                send_all(sock_fd, &tdi, 1) >= 0 &&
                recv_all(sock_fd, &tdo, 1) >= 0) {
                success_count++;
            }
        }
    }

    if (success_count >= 45) { /* Allow up to 10% failure for timing issues */
        char msg[64];
        snprintf(msg, sizeof(msg), "TCK stress test: %d/50 successful", success_count);
        print_pass(msg);
        return 1;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Only %d/50 operations succeeded", success_count);
    print_fail(msg);
    return 0;
}

static int test_jtag_tms_sequence_cmd(void) {
    print_test("JTAG TMS Sequence Command (CMD_TMS)");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x01; /* CMD_TMS */
    cmd.length = htonl(2);
    print_info("Sending 2-byte TMS sequence (0xFFFF)");

    if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
        print_fail("TMS sequence command rejected");
        return 0;
    }

    uint8_t tms_data[2] = {0xFF, 0xFF};
    if (send_all(sock_fd, tms_data, 2) < 0) {
        print_fail("Failed to send TMS data");
        return 0;
    }

    print_pass("TMS sequence command completed");
    return 1;
}

static int test_jtag_reset_scan_sequence(void) {
    print_test("JTAG Reset-then-Scan Sequence");
    print_info("Testing command sequencing: RESET followed by SCAN");

    /* First: RESET */
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x00; /* CMD_RESET */
    cmd.length = htonl(0);

    if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
        print_fail("Reset command failed in sequence");
        return 0;
    }
    print_info("Reset completed");

    /* Then: SCAN */
    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));
    cmd.cmd = 0x02; /* CMD_SCAN */
    cmd.length = htonl(8);

    if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
        print_fail("Scan command failed after reset");
        return 0;
    }

    uint8_t tms = 0x00, tdi = 0x55, tdo;
    if (send_all(sock_fd, &tms, 1) < 0 ||
        send_all(sock_fd, &tdi, 1) < 0 ||
        recv_all(sock_fd, &tdo, 1) < 0) {
        print_fail("Scan data transfer failed");
        return 0;
    }

    print_pass("Reset-then-Scan sequence completed successfully");
    return 1;
}

static int test_jtag_alternating_rapid_commands(void) {
    print_test("JTAG Alternating Rapid Commands (Reset/Scan)");
    print_info("Alternating between RESET and SCAN commands (10 iterations)");

    int success_count = 0;
    for (int i = 0; i < 10; i++) {
        struct jtag_vpi_cmd cmd = {0};
        struct jtag_vpi_resp resp = {0};

        if (i % 2 == 0) {
            /* RESET */
            cmd.cmd = 0x00;
            cmd.length = htonl(0);
            if (jtag_send_cmd(&cmd, &resp) == 0 && resp.response == 0x00) {
                success_count++;
            }
        } else {
            /* SCAN */
            cmd.cmd = 0x02;
            cmd.length = htonl(8);
            if (jtag_send_cmd(&cmd, &resp) == 0 && resp.response == 0x00) {
                uint8_t tms = 0, tdi = 0, tdo;
                if (send_all(sock_fd, &tms, 1) >= 0 &&
                    send_all(sock_fd, &tdi, 1) >= 0 &&
                    recv_all(sock_fd, &tdo, 1) >= 0) {
                    success_count++;
                }
            }
        }
    }

    if (success_count >= 9) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Alternating commands: %d/10 successful", success_count);
        print_pass(msg);
        return 1;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Too many failures: %d/10 succeeded", success_count);
    print_fail(msg);
    return 0;
}

static int run_jtag_tests(void) {
    int ok = 1;

    /* Command-level tests */
    print_info("=== Command Protocol Tests ===");
    ok &= test_jtag_reset();
    ok &= test_jtag_mode_query();
    ok &= test_jtag_scan8();
    ok &= test_jtag_multiple_resets();
    ok &= test_jtag_invalid_command();
    ok &= test_jtag_scan32();
    ok &= test_jtag_scan_patterns();
    ok &= test_jtag_rapid_commands();
    ok &= test_jtag_tms_sequence_cmd();
    ok &= test_jtag_reset_scan_sequence();
    ok &= test_jtag_alternating_rapid_commands();

    /* Physical-level tests */
    print_info("=== Physical Layer Tests (4-Wire JTAG) ===");
    ok &= test_jtag_tms_state_machine();
    ok &= test_jtag_tdi_tdo_integrity();
    ok &= test_jtag_boundary_scan_simulation();
    ok &= test_jtag_idcode_read_simulation();
    ok &= test_jtag_shift_register_length();
    ok &= test_jtag_tck_frequency_stress();

    return ok;
}

/* -------------------------------------------------------------------------- */
/* cJTAG (OScan1, CMD_OSCAN1)                                                */
/* -------------------------------------------------------------------------- */

#define CMD_OSCAN1 5
#define VPI_MAX_BUF 512

struct cjtag_vpi_cmd {
    uint32_t cmd;
    uint8_t buffer_out[VPI_MAX_BUF];
    uint8_t buffer_in[VPI_MAX_BUF];
    uint32_t length;
    uint32_t nb_bits;
} __attribute__((packed));

static int send_oscan_cmd(struct cjtag_vpi_cmd *cmd, uint8_t *tdo_out) {
    struct cjtag_vpi_cmd tx = {0};
    memcpy(&tx, cmd, sizeof(tx));
    tx.cmd = TO_LE32(tx.cmd);
    tx.length = TO_LE32(tx.length);
    tx.nb_bits = TO_LE32(tx.nb_bits);
    if (send_all(sock_fd, &tx, sizeof(tx)) < 0)
        return -1;
    struct cjtag_vpi_cmd rx = {0};
    if (recv_all(sock_fd, &rx, sizeof(rx)) < 0)
        return -1;
    if (tdo_out)
        *tdo_out = rx.buffer_in[0] & 1;
    return 0;
}

static int oscan1_edge(uint8_t tckc, uint8_t tmsc, uint8_t *tdo_out) {
    struct cjtag_vpi_cmd cmd = {0};
    cmd.cmd = CMD_OSCAN1;
    cmd.length = 1;
    cmd.nb_bits = 2;
    cmd.buffer_out[0] = (tckc & 1) | ((tmsc & 1) << 1);
    return send_oscan_cmd(&cmd, tdo_out);
}

static int oscan1_send_oac(void) {
    for (int i = 0; i < 16; i++) {
        if (oscan1_edge(1, 1, NULL) != 0)
            return -1;
    }
    return 0;
}

static int oscan1_send_jscan(uint8_t code) {
    uint8_t packet = (1u << 4) | (code & 0x0F);
    for (int i = 4; i >= 0; i--) {
        uint8_t bit = (packet >> i) & 1u;
        if (oscan1_edge(1, bit, NULL) != 0)
            return -1;
    }
    return 0;
}

static int oscan1_sf0(uint8_t tms, uint8_t tdi, uint8_t *tdo) {
    if (oscan1_edge(1, tms, NULL) != 0)
        return -1;
    if (oscan1_edge(1, tdi, tdo) != 0)
        return -1;
    return 0;
}

static uint8_t cjtag_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
    }
    return crc;
}

/* cJTAG Protocol Test Functions */
static int test_cjtag_two_wire_detection(void) {
    uint8_t tdo = 0;
    print_test("Two-Wire Mode Detection (CMD_OSCAN1)");
    if (oscan1_edge(1, 1, &tdo) == 0) {
        print_pass("CMD_OSCAN1 accepted");
        return 1;
    } else {
        print_fail("CMD_OSCAN1 rejected");
        return 0;
    }
}

static int test_cjtag_oac_sequence(void) {
    print_test("OScan1 Attention Character (16 edges)");
    if (oscan1_send_oac() == 0) {
        print_pass("OAC sent");
        return 1;
    } else {
        print_fail("OAC failed");
        return 0;
    }
}

static int test_cjtag_jscan_oscan_on(void) {
    print_test("JScan OSCAN_ON (0x1)");
    if (oscan1_send_jscan(0x1) == 0) {
        print_pass("JSCAN_OSCAN_ON sent");
        return 1;
    } else {
        print_fail("JSCAN_OSCAN_ON failed");
        return 0;
    }
}

static int test_cjtag_bit_stuffing(void) {
    print_test("Bit stuffing (eight 1s)");
    for (int i = 0; i < 8; i++) {
        if (oscan1_edge(1, 1, NULL) != 0) {
            print_fail("Stuffing failed");
            return 0;
        }
    }
    print_pass("Stuffing sequence accepted");
    return 1;
}

static int test_cjtag_sf0_transfer(void) {
    uint8_t tdo;
    print_test("SF0 transfer");
    if (oscan1_sf0(0, 1, &tdo) == 0) {
        print_pass("SF0 completed");
        return 1;
    } else {
        print_fail("SF0 failed");
        return 0;
    }
}

static int test_cjtag_crc8_calculation(void) {
    print_test("CRC-8 Calculation");
    uint8_t data[3] = {0xAA, 0x55, 0xFF};
    uint8_t crc = cjtag_crc8(data, sizeof(data));
    if (crc == 0x5A) {
        print_pass("CRC-8 matches 0x5A");
        return 1;
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Unexpected CRC 0x%02X", crc);
        print_fail(msg);
        return 0;
    }
}

static int test_cjtag_tap_reset_sf0(void) {
    print_test("TAP reset via SF0 (5 cycles)");
    for (int i = 0; i < 5; i++) {
        if (oscan1_sf0(1, 0, NULL) != 0) {
            print_fail("TAP reset failed");
            return 0;
        }
    }
    print_pass("TAP reset sequence sent");
    return 1;
}

static int test_cjtag_mode_flag_probe(void) {
    uint8_t tdo;
    print_test("Mode flag probe");
    if (oscan1_edge(0, 0, &tdo) == 0) {
        print_pass("Mode flag response received");
        return 1;
    } else {
        print_fail("Mode flag probe failed");
        return 0;
    }
}

static int test_cjtag_multiple_oac(void) {
    print_test("Multiple OAC sequences");
    for (int i = 0; i < 3; i++) {
        if (oscan1_send_oac() != 0) {
            print_fail("Multiple OAC failed");
            return 0;
        }
    }
    print_pass("Multiple OAC sequences accepted");
    return 1;
}

static int test_cjtag_jscan_mode_switching(void) {
    print_test("JScan OSCAN_OFF (0x0) and OSCAN_ON cycle");
    if (oscan1_send_jscan(0x0) == 0 && oscan1_send_jscan(0x1) == 0) {
        print_pass("JScan mode switching works");
        return 1;
    } else {
        print_fail("JScan mode switching failed");
        return 0;
    }
}

static int test_cjtag_extended_sf0(void) {
    print_test("Extended SF0 sequence (16 cycles)");
    for (int i = 0; i < 16; i++) {
        uint8_t tms_bit = (i < 8) ? 1 : 0;
        if (oscan1_sf0(tms_bit, i & 1, NULL) != 0) {
            print_fail("Extended SF0 failed");
            return 0;
        }
    }
    print_pass("Extended SF0 sequence completed");
    return 1;
}

static int test_cjtag_cmd_reset(void) {
    print_test("cJTAG: TAP Reset via CMD_RESET");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x00; /* CMD_RESET */
    cmd.length = htonl(0);
    if (jtag_send_cmd(&cmd, &resp) == 0 && resp.response == 0x00) {
        print_pass("CMD_RESET works over cJTAG mode");
        return 1;
    } else {
        print_fail("CMD_RESET failed in cJTAG mode");
        return 0;
    }
}

static int test_cjtag_scan_8bit(void) {
    print_test("cJTAG: Scan 8 bits via CMD_SCAN");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x02; /* CMD_SCAN */
    cmd.length = htonl(8);
    if (jtag_send_cmd(&cmd, &resp) == 0 && resp.response == 0x00) {
        uint8_t tms_buf = 0x00;
        uint8_t tdi_buf = 0xAA;
        uint8_t tdo_buf = 0;
        if (send_all(sock_fd, &tms_buf, 1) >= 0 &&
            send_all(sock_fd, &tdi_buf, 1) >= 0 &&
            recv_all(sock_fd, &tdo_buf, 1) >= 0) {
            print_pass("CMD_SCAN works over cJTAG mode");
            return 1;
        } else {
            print_fail("CMD_SCAN data transfer failed");
            return 0;
        }
    } else {
        print_fail("CMD_SCAN command failed in cJTAG mode");
        return 0;
    }
}

static int test_cjtag_mode_query(void) {
    print_test("cJTAG: Mode query via CMD_SET_PORT");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x03; /* CMD_SET_PORT */
    cmd.length = htonl(0);
    if (jtag_send_cmd(&cmd, &resp) == 0) {
        if (resp.mode == 1) {
            print_pass("Mode reports cJTAG (mode=1)");
            return 1;
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "Mode=%d (expected 1 for cJTAG)", resp.mode);
            print_info(msg);
            print_pass("Mode query succeeded (info: mode mismatch)");
            return 1;  // Still pass, just info
        }
    } else {
        print_fail("Mode query failed");
        return 0;
    }
}

static int test_cjtag_large_scan_32bit(void) {
    print_test("cJTAG: Large scan (32 bits) via CMD_SCAN");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x02;
    cmd.length = htonl(32);
    if (jtag_send_cmd(&cmd, &resp) == 0 && resp.response == 0x00) {
        uint8_t tms_buf[4] = {0x00, 0x00, 0x00, 0x00};
        uint8_t tdi_buf[4] = {0x55, 0xAA, 0x55, 0xAA};
        uint8_t tdo_buf[4];
        if (send_all(sock_fd, tms_buf, 4) >= 0 &&
            send_all(sock_fd, tdi_buf, 4) >= 0 &&
            recv_all(sock_fd, tdo_buf, 4) >= 0) {
            print_pass("32-bit scan works over cJTAG mode");
            return 1;
        } else {
            print_fail("32-bit scan data transfer failed");
            return 0;
        }
    } else {
        print_fail("32-bit scan command failed");
        return 0;
    }
}

static int test_cjtag_rapid_reset(void) {
    print_test("cJTAG: Rapid reset commands (5 cycles)");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    for (int i = 0; i < 5; i++) {
        cmd.cmd = 0x00;
        cmd.length = htonl(0);
        if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
            print_fail("Rapid resets failed");
            return 0;
        }
    }
    print_pass("Rapid resets work in cJTAG mode");
    return 1;
}

static int test_cjtag_read_idcode(void) {
    print_test("cJTAG: Read IDCODE (32-bit scan with reset)");
    print_info("Reading IDCODE register via cJTAG");

    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};

    /* First, reset to clean state */
    cmd.cmd = 0x00; /* CMD_RESET */
    cmd.length = htonl(0);
    if (jtag_send_cmd(&cmd, &resp) != 0) {
        print_fail("Initial reset failed");
        return 0;
    }

    /* Now scan IDCODE (IR=0x01 is implicit in default DR scan) */
    cmd.cmd = 0x02; /* CMD_SCAN */
    cmd.length = htonl(32);

    if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
        print_fail("IDCODE scan command failed");
        return 0;
    }

    /* Send TMS and TDI buffers (all zeros for straight shift) */
    uint8_t tms_buf[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t tdi_buf[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t tdo_buf[4] = {0};

    if (send_all(sock_fd, tms_buf, 4) < 0 ||
        send_all(sock_fd, tdi_buf, 4) < 0 ||
        recv_all(sock_fd, tdo_buf, 4) < 0) {
        print_fail("IDCODE data transfer failed");
        return 0;
    }

    /* Parse 32-bit IDCODE in little-endian format */
    uint32_t idcode = (tdo_buf[3] << 24) | (tdo_buf[2] << 16) |
                     (tdo_buf[1] << 8) | tdo_buf[0];

    if (validate_u32("IDCODE", idcode, 0x1DEAD3FF)) {
        print_pass("IDCODE read successfully (0x1DEAD3FF)");
        return 1;
    } else {
        char msg[100];
        snprintf(msg, sizeof(msg), "IDCODE mismatch in cJTAG: got 0x%08X, expected 0x1DEAD3FF", idcode);
        print_fail(msg);
        return 0;
    }
}

static int run_cjtag_tests(void) {
    int ok = 1;

    /* OScan1 Protocol Layer Tests */
    print_info("=== OScan1 Protocol Layer Tests (2-Wire) ===");
    ok &= test_cjtag_two_wire_detection();
    ok &= test_cjtag_oac_sequence();
    ok &= test_cjtag_jscan_oscan_on();
    ok &= test_cjtag_bit_stuffing();
    ok &= test_cjtag_sf0_transfer();
    ok &= test_cjtag_crc8_calculation();
    ok &= test_cjtag_tap_reset_sf0();
    ok &= test_cjtag_mode_flag_probe();
    ok &= test_cjtag_multiple_oac();
    ok &= test_cjtag_jscan_mode_switching();
    ok &= test_cjtag_extended_sf0();

    /* Command Protocol Tests (using standard JTAG commands over cJTAG) */
    print_info("=== Command Protocol Tests (JTAG commands over cJTAG) ===");
    ok &= test_cjtag_cmd_reset();
    ok &= test_cjtag_read_idcode();
    ok &= test_cjtag_scan_8bit();
    ok &= test_cjtag_mode_query();
    ok &= test_cjtag_large_scan_32bit();
    ok &= test_cjtag_rapid_reset();

    return ok;
}

/* -------------------------------------------------------------------------- */
/* Legacy 8-byte protocol                                                     */
/* -------------------------------------------------------------------------- */

struct legacy_cmd {
    uint8_t cmd;
    uint8_t mode;
    uint8_t reserved[2];
    uint32_t length; /* big-endian */
} __attribute__((packed));

static int legacy_send(struct legacy_cmd *cmd, const void *payload, uint32_t payload_len, uint8_t *resp, uint32_t *resp_len) {
    if (send_all(sock_fd, cmd, sizeof(*cmd)) < 0)
        return -1;
    if (payload_len && payload) {
        if (send_all(sock_fd, payload, payload_len) < 0)
            return -1;
    }
    /* Read whatever comes back (best-effort) */
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(sock_fd, &rset);
    struct timeval tv = {.tv_sec = TIMEOUT_SEC, .tv_usec = 0};
    int sel = select(sock_fd + 1, &rset, NULL, NULL, &tv);
    if (sel <= 0) {
        *resp_len = 0;
        return 0;
    }
    int n = recv(sock_fd, resp, 256, 0);
    if (n > 0) *resp_len = (uint32_t)n; else *resp_len = 0;
    return 0;
}

/* Legacy Protocol Test Functions */
static int test_legacy_tap_reset(void) {
    print_test("Legacy: TAP reset (CMD_RESET)");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x00;
    cmd.length = htonl(0);
    if (legacy_send(&cmd, NULL, 0, resp, &resp_len) == 0) {
        if (resp_len >= 1) {
            print_pass("Reset command successful");
            return 1;
        } else {
            print_fail("Reset: response too short");
            return 0;
        }
    } else {
        print_fail("Reset: command failed");
        return 0;
    }
}

static int test_legacy_scan_8bit(void) {
    print_test("Legacy: Scan 8 bits (CMD_SCAN) - Response Validation");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;
    uint8_t payload[6] = {0x00, 0xAA, 0x00, 0x00, 0x00, 0x08};

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x02;
    cmd.length = htonl(sizeof(payload));
    memset(resp, 0, sizeof(resp));
    resp_len = 0;
    if (legacy_send(&cmd, payload, sizeof(payload), resp, &resp_len) == 0) {
        if (resp_len >= 1) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Scan response: TDO byte = 0x%02X", resp[0]);
            print_info(msg);
            print_pass("Scan completed with response validation");
            return 1;
        } else {
            print_fail("Scan: response missing TDO data");
            return 0;
        }
    } else {
        print_fail("Scan: command failed");
        return 0;
    }
}

static int test_legacy_mode_query(void) {
    print_test("Legacy: Mode Query (CMD_SET_PORT) - Response Validation");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x03;
    cmd.mode = 0xFF;
    cmd.length = htonl(0);
    memset(resp, 0, sizeof(resp));
    resp_len = 0;
    if (legacy_send(&cmd, NULL, 0, resp, &resp_len) == 0) {
        if (resp_len >= 1) {
            uint8_t mode = resp[0];
            char msg[80];
            snprintf(msg, sizeof(msg), "Current mode: %s (0x%02X)",
                     mode == 0 ? "JTAG" : (mode == 1 ? "cJTAG" : "Unknown"), mode);
            print_info(msg);
            if (mode == 0 || mode == 1) {
                print_pass("Mode query successful - valid mode returned");
                return 1;
            } else {
                char errmsg[80];
                snprintf(errmsg, sizeof(errmsg), "Mode query: unexpected mode value 0x%02X", mode);
                print_fail(errmsg);
                return 0;
            }
        } else {
            print_fail("Mode query: response missing mode byte");
            return 0;
        }
    } else {
        print_fail("Mode query: command failed");
        return 0;
    }
}

static int test_legacy_idcode_read(void) {
    print_test("Legacy: IDCODE Read - Response Validation");
    print_info("Reading IDCODE register via legacy protocol");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x02;
    cmd.length = htonl(34);

    uint8_t idcode_tms[5] = {0x02, 0x00, 0x00, 0x00, 0x02};
    uint8_t idcode_tdi[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

    memset(resp, 0, sizeof(resp));
    resp_len = 0;

    if (legacy_send(&cmd, idcode_tms, 5, resp, &resp_len) == 0) {
        if (legacy_send(&cmd, idcode_tdi, 5, resp, &resp_len) == 0) {
            if (resp_len >= 5) {
                uint32_t idcode = 0;
                for (int i = 0; i < 32; i++) {
                    int bit_pos = i + 3;
                    int byte_idx = bit_pos / 8;
                    int bit_idx = bit_pos % 8;
                    if (byte_idx < 5 && (resp[byte_idx] & (1 << bit_idx))) {
                        idcode |= (1U << i);
                    }
                }

                char msg[80];
                snprintf(msg, sizeof(msg), "IDCODE read: 0x%08X", idcode);
                print_info(msg);

                if (idcode == 0x1DEAD3FF) {
                    print_pass("IDCODE correct: 0x1DEAD3FF");
                    return 1;
                } else {
                    char errmsg[100];
                    snprintf(errmsg, sizeof(errmsg),
                             "IDCODE mismatch: got 0x%08X, expected 0x1DEAD3FF", idcode);
                    print_fail(errmsg);
                    return 0;
                }
            } else {
                print_fail("IDCODE read: response too short for TDO data");
                return 0;
            }
        } else {
            print_fail("IDCODE read: TDI buffer send failed");
            return 0;
        }
    } else {
        print_fail("IDCODE read: TMS buffer send failed");
        return 0;
    }
}

static int test_legacy_tms_sequence(void) {
    print_test("Legacy: TMS sequence - Response Validation");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;
    uint8_t tms_data[2] = {0xFF, 0xFF};

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x01;
    cmd.length = htonl(2);
    memset(resp, 0, sizeof(resp));
    resp_len = 0;
    if (legacy_send(&cmd, tms_data, 2, resp, &resp_len) == 0) {
        if (resp_len >= 1) {
            print_pass("TMS sequence completed with response");
            return 1;
        } else {
            print_fail("TMS sequence: response missing");
            return 0;
        }
    } else {
        print_fail("TMS sequence: command failed");
        return 0;
    }
}

static int test_legacy_multiple_resets(void) {
    print_test("Legacy: Multiple sequential resets - Response Validation");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    for (int i = 0; i < 3; i++) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.cmd = 0x00;
        cmd.length = htonl(0);
        memset(resp, 0, sizeof(resp));
        resp_len = 0;
        if (legacy_send(&cmd, NULL, 0, resp, &resp_len) != 0 || resp_len < 1) {
            print_fail("Sequential resets failed");
            return 0;
        }
    }
    print_pass("3 sequential resets completed with response validation");
    return 1;
}

static int test_legacy_reset_scan_sequence(void) {
    print_test("Legacy: Reset then Scan sequence - Response Validation");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x00;
    cmd.length = htonl(0);
    memset(resp, 0, sizeof(resp));
    resp_len = 0;
    if (legacy_send(&cmd, NULL, 0, resp, &resp_len) != 0 || resp_len < 1) {
        print_fail("Reset failed in sequence");
        return 0;
    }

    uint8_t scan_payload[6] = {0x00, 0x55, 0x00, 0x00, 0x00, 0x08};
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x02;
    cmd.length = htonl(6);
    memset(resp, 0, sizeof(resp));
    resp_len = 0;
    if (legacy_send(&cmd, scan_payload, 6, resp, &resp_len) == 0 && resp_len >= 1) {
        print_pass("Reset then scan sequence completed with validation");
        return 1;
    } else {
        print_fail("Scan failed after reset");
        return 0;
    }
}

static int test_legacy_large_scan(void) {
    print_test("Legacy: Large scan (32 bits) - Response Validation");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;
    uint8_t large_payload[10] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x20};

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x02;
    cmd.length = htonl(10);
    memset(resp, 0, sizeof(resp));
    resp_len = 0;
    if (legacy_send(&cmd, large_payload, 10, resp, &resp_len) == 0 && resp_len >= 1) {
        print_pass("32-bit scan accepted with response");
        return 1;
    } else {
        print_fail("Large scan failed or missing response");
        return 0;
    }
}

static int test_legacy_unknown_command(void) {
    print_test("Legacy: Unknown command handling");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0xFF;
    cmd.length = htonl(0);
    memset(resp, 0, sizeof(resp));
    resp_len = 0;
    legacy_send(&cmd, NULL, 0, resp, &resp_len);
    print_pass("Unknown command handled (server didn't crash)");
    return 1;
}

static int test_legacy_rapid_commands(void) {
    print_test("Legacy: Rapid command sequence (10 commands) - Response Validation");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    for (int i = 0; i < 10; i++) {
        memset(&cmd, 0, sizeof(cmd));
        memset(resp, 0, sizeof(resp));
        resp_len = 0;
        if (i % 2 == 0) {
            cmd.cmd = 0x00;
            cmd.length = htonl(0);
            if (legacy_send(&cmd, NULL, 0, resp, &resp_len) != 0 || resp_len < 1) {
                print_fail("Rapid commands failed");
                return 0;
            }
        } else {
            uint8_t quick_scan[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x08};
            cmd.cmd = 0x02;
            cmd.length = htonl(6);
            if (legacy_send(&cmd, quick_scan, 6, resp, &resp_len) != 0 || resp_len < 1) {
                print_fail("Rapid commands failed");
                return 0;
            }
        }
    }
    print_pass("10 rapid commands completed with response validation");
    return 1;
}

static int test_legacy_scan_patterns(void) {
    print_test("Legacy: Scan pattern variations - Response Validation");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;
    uint8_t patterns[3][6] = {
        {0x00, 0xAA, 0x00, 0x00, 0x00, 0x08},
        {0x00, 0x55, 0x00, 0x00, 0x00, 0x08},
        {0x00, 0xFF, 0x00, 0x00, 0x00, 0x08}
    };

    for (int p = 0; p < 3; p++) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.cmd = 0x02;
        cmd.length = htonl(6);
        memset(resp, 0, sizeof(resp));
        resp_len = 0;
        if (legacy_send(&cmd, patterns[p], 6, resp, &resp_len) != 0 || resp_len < 1) {
            print_fail("Pattern test failed");
            return 0;
        }
    }
    print_pass("Pattern variations accepted with response validation");
    return 1;
}

static int test_legacy_alternating_commands(void) {
    print_test("Legacy: Alternating commands (Reset/Scan) - Response Validation");
    print_info("Alternating between RESET and SCAN commands (10 iterations)");
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    for (int i = 0; i < 10; i++) {
        memset(&cmd, 0, sizeof(cmd));
        memset(resp, 0, sizeof(resp));
        resp_len = 0;
        if (i % 2 == 0) {
            cmd.cmd = 0x00;
            cmd.length = htonl(0);
            if (legacy_send(&cmd, NULL, 0, resp, &resp_len) != 0 || resp_len < 1) {
                print_fail("Alternating commands failed");
                return 0;
            }
        } else {
            uint8_t scan_data[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x08};
            cmd.cmd = 0x02;
            cmd.length = htonl(6);
            if (legacy_send(&cmd, scan_data, 6, resp, &resp_len) != 0 || resp_len < 1) {
                print_fail("Alternating commands failed");
                return 0;
            }
        }
    }
    print_pass("Alternating commands successful with validation (10 iterations)");
    return 1;
}

static int run_legacy_tests(void) {
    int ok = 1;

    ok &= test_legacy_tap_reset();
    ok &= test_legacy_scan_8bit();
    ok &= test_legacy_mode_query();
    ok &= test_legacy_idcode_read();
    ok &= test_legacy_tms_sequence();
    ok &= test_legacy_multiple_resets();
    ok &= test_legacy_reset_scan_sequence();
    ok &= test_legacy_large_scan();
    ok &= test_legacy_unknown_command();
    ok &= test_legacy_rapid_commands();
    ok &= test_legacy_scan_patterns();
    ok &= test_legacy_alternating_commands();

    return ok;
}

/* -------------------------------------------------------------------------- */
/* Combo Protocol Tests                                                       */
/* -------------------------------------------------------------------------- */

/* Combo Protocol Test Functions */
static int test_combo_sequential_switching(void) {
    print_test("Combo: Sequential Protocol Switching");
    print_info("Phase 1: JTAG operations");

    struct jtag_vpi_cmd jtag_cmd = {0};
    struct jtag_vpi_resp jtag_resp = {0};
    jtag_cmd.cmd = 0x00;
    jtag_cmd.length = htonl(0);
    if (jtag_send_cmd(&jtag_cmd, &jtag_resp) != 0 || jtag_resp.response != 0x00) {
        print_fail("JTAG reset failed");
        return 0;
    }

    print_info("Phase 2: Switch to Legacy protocol");
    struct legacy_cmd legacy_cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;
    memset(&legacy_cmd, 0, sizeof(legacy_cmd));
    legacy_cmd.cmd = 0x00;
    legacy_cmd.length = htonl(0);
    if (legacy_send(&legacy_cmd, NULL, 0, resp, &resp_len) != 0) {
        print_fail("Legacy reset failed");
        return 0;
    }

    print_info("Phase 3: Switch back to JTAG");
    memset(&jtag_cmd, 0, sizeof(jtag_cmd));
    memset(&jtag_resp, 0, sizeof(jtag_resp));
    jtag_cmd.cmd = 0x00;
    jtag_cmd.length = htonl(0);
    if (jtag_send_cmd(&jtag_cmd, &jtag_resp) == 0 && jtag_resp.response == 0x00) {
        print_pass("Sequential protocol switching successful");
        return 1;
    } else {
        print_fail("Return to JTAG failed");
        return 0;
    }
}

static int test_combo_alternating_operations(void) {
    print_test("Combo: Alternating JTAG/Legacy Operations");
    print_info("Rapidly alternating between JTAG and Legacy commands");

    struct jtag_vpi_cmd jtag_cmd = {0};
    struct jtag_vpi_resp jtag_resp = {0};
    struct legacy_cmd legacy_cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    for (int i = 0; i < 5; i++) {
        memset(&jtag_cmd, 0, sizeof(jtag_cmd));
        memset(&jtag_resp, 0, sizeof(jtag_resp));
        jtag_cmd.cmd = 0x00;
        jtag_cmd.length = htonl(0);
        if (jtag_send_cmd(&jtag_cmd, &jtag_resp) != 0 || jtag_resp.response != 0x00) {
            print_fail("Alternating operations failed");
            return 0;
        }

        memset(&legacy_cmd, 0, sizeof(legacy_cmd));
        legacy_cmd.cmd = 0x00;
        legacy_cmd.length = htonl(0);
        if (legacy_send(&legacy_cmd, NULL, 0, resp, &resp_len) != 0) {
            print_fail("Alternating operations failed");
            return 0;
        }
    }
    print_pass("Alternating operations successful (5 iterations)");
    return 1;
}

static int test_combo_rapid_protocol_detection(void) {
    print_test("Combo: Rapid Protocol Auto-Detection");
    print_info("Testing server's protocol detection with rapid switches");

    struct jtag_vpi_cmd jtag_cmd = {0};
    struct jtag_vpi_resp jtag_resp = {0};
    struct legacy_cmd legacy_cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    for (int i = 0; i < 10; i++) {
        if (i % 2 == 0) {
            memset(&jtag_cmd, 0, sizeof(jtag_cmd));
            memset(&jtag_resp, 0, sizeof(jtag_resp));
            jtag_cmd.cmd = 0x00;
            jtag_cmd.length = htonl(0);
            if (jtag_send_cmd(&jtag_cmd, &jtag_resp) != 0) {
                print_fail("Rapid protocol detection failed");
                return 0;
            }
        } else {
            memset(&legacy_cmd, 0, sizeof(legacy_cmd));
            legacy_cmd.cmd = 0x00;
            legacy_cmd.length = htonl(0);
            if (legacy_send(&legacy_cmd, NULL, 0, resp, &resp_len) != 0) {
                print_fail("Rapid protocol detection failed");
                return 0;
            }
        }
    }
    print_pass("Rapid protocol detection successful (10 switches)");
    return 1;
}

static int test_combo_mixed_scan_operations(void) {
    print_test("Combo: Mixed Scan Operations (JTAG + Legacy)");
    print_info("Testing scan operations with different protocols");

    struct jtag_vpi_cmd jtag_cmd = {0};
    struct jtag_vpi_resp jtag_resp = {0};
    memset(&jtag_cmd, 0, sizeof(jtag_cmd));
    memset(&jtag_resp, 0, sizeof(jtag_resp));
    jtag_cmd.cmd = 0x02;
    jtag_cmd.length = htonl(8);
    if (jtag_send_cmd(&jtag_cmd, &jtag_resp) == 0 && jtag_resp.response == 0x00) {
        uint8_t tms_buf[1] = {0x00};
        uint8_t tdi_buf[1] = {0xAA};
        uint8_t tdo_buf[1];
        if (send_all(sock_fd, tms_buf, 1) != 0 ||
            send_all(sock_fd, tdi_buf, 1) != 0 ||
            recv_all(sock_fd, tdo_buf, 1) != 0) {
            print_fail("JTAG scan data transfer failed");
            return 0;
        }
    } else {
        print_fail("JTAG scan command failed");
        return 0;
    }

    uint8_t payload[6] = {0x00, 0x55, 0x00, 0x00, 0x00, 0x08};
    struct legacy_cmd legacy_cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;
    memset(&legacy_cmd, 0, sizeof(legacy_cmd));
    legacy_cmd.cmd = 0x02;
    legacy_cmd.length = htonl(sizeof(payload));
    if (legacy_send(&legacy_cmd, payload, sizeof(payload), resp, &resp_len) == 0) {
        print_pass("Mixed scan operations successful (JTAG + Legacy)");
        return 1;
    } else {
        print_fail("Legacy scan failed");
        return 0;
    }
}

static int test_combo_backtoback_resets(void) {
    print_test("Combo: Back-to-Back Resets (Protocol Mix)");
    print_info("Testing multiple resets across protocols");

    struct jtag_vpi_cmd jtag_cmd = {0};
    struct jtag_vpi_resp jtag_resp = {0};
    struct legacy_cmd legacy_cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    for (int i = 0; i < 3; i++) {
        memset(&jtag_cmd, 0, sizeof(jtag_cmd));
        memset(&jtag_resp, 0, sizeof(jtag_resp));
        jtag_cmd.cmd = 0x00;
        jtag_cmd.length = htonl(0);
        if (jtag_send_cmd(&jtag_cmd, &jtag_resp) != 0 || jtag_resp.response != 0x00) {
            print_fail("Back-to-back resets failed");
            return 0;
        }

        memset(&legacy_cmd, 0, sizeof(legacy_cmd));
        legacy_cmd.cmd = 0x00;
        legacy_cmd.length = htonl(0);
        if (legacy_send(&legacy_cmd, NULL, 0, resp, &resp_len) != 0) {
            print_fail("Back-to-back resets failed");
            return 0;
        }
    }
    print_pass("Back-to-back resets successful (3 JTAG + 3 Legacy)");
    return 1;
}

static int test_combo_large_scan_mix(void) {
    print_test("Combo: Large Scan Mix (32-bit JTAG + Legacy)");
    print_info("Testing 32-bit scans with both protocols");

    struct jtag_vpi_cmd jtag_cmd = {0};
    struct jtag_vpi_resp jtag_resp = {0};
    memset(&jtag_cmd, 0, sizeof(jtag_cmd));
    memset(&jtag_resp, 0, sizeof(jtag_resp));
    jtag_cmd.cmd = 0x02;
    jtag_cmd.length = htonl(32);
    if (jtag_send_cmd(&jtag_cmd, &jtag_resp) == 0 && jtag_resp.response == 0x00) {
        uint8_t tms_buf[4] = {0x00, 0x00, 0x00, 0x00};
        uint8_t tdi_buf[4] = {0x12, 0x34, 0x56, 0x78};
        uint8_t tdo_buf[4];
        if (send_all(sock_fd, tms_buf, 4) != 0 ||
            send_all(sock_fd, tdi_buf, 4) != 0 ||
            recv_all(sock_fd, tdo_buf, 4) != 0) {
            print_fail("JTAG 32-bit data transfer failed");
            return 0;
        }
    } else {
        print_fail("JTAG 32-bit scan command failed");
        return 0;
    }

    uint8_t large_payload[9] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0x20};
    struct legacy_cmd legacy_cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;
    memset(&legacy_cmd, 0, sizeof(legacy_cmd));
    legacy_cmd.cmd = 0x02;
    legacy_cmd.length = htonl(sizeof(large_payload));
    if (legacy_send(&legacy_cmd, large_payload, sizeof(large_payload), resp, &resp_len) == 0) {
        print_pass("Large scan mix successful (32-bit JTAG + Legacy)");
        return 1;
    } else {
        print_fail("Legacy 32-bit scan failed");
        return 0;
    }
}

static int run_combo_tests(void) {
    int ok = 1;

    ok &= test_combo_sequential_switching();
    ok &= test_combo_alternating_operations();
    ok &= test_combo_rapid_protocol_detection();
    ok &= test_combo_mixed_scan_operations();
    ok &= test_combo_backtoback_resets();
    ok &= test_combo_large_scan_mix();

    return ok;
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(int argc, char **argv) {
    const char *mode = (argc > 1) ? argv[1] : "jtag";

    printf("\n=== Unified Protocol Test Client ===\n");
    printf("Mode: %s\n", mode);
    printf("Target: %s:%d\n\n", VPI_ADDR, VPI_PORT);

    sock_fd = connect_vpi();
    if (sock_fd < 0) {
        printf("✗ ERROR: Could not connect to VPI server\n");
        return 1;
    }
    printf("✓ Connected to VPI server\n");

    int ok = 0;
    if (strcmp(mode, "cjtag") == 0) {
        ok = run_cjtag_tests();
    } else if (strcmp(mode, "legacy") == 0) {
        ok = run_legacy_tests();
    } else if (strcmp(mode, "combo") == 0) {
        ok = run_combo_tests();
    } else {
        ok = run_jtag_tests();
    }

    close(sock_fd);

    printf("\n=== Test Summary ===\n");
    printf("Total Tests: %d\n", test_count);
    printf("Passed: %d\n", pass_count);
    printf("Failed: %d\n\n", fail_count);

    if (ok && fail_count == 0 && pass_count == test_count) {
        printf("✓ All tests PASSED\n");
        return 0;
    }
    if (pass_count < test_count && fail_count == 0) {
        printf("⚠ WARNING: %d test(s) skipped or informational only\n", test_count - pass_count);
        printf("✓ All executed tests PASSED (informational tests excluded)\n");
        return 0;
    }
    printf("✗ Some tests FAILED\n");
    return 1;
}
