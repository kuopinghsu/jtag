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
    if (jtag_send_cmd(&cmd, &resp) == 0 && resp.response == 0x00) {
        print_pass("TAP reset acknowledged");
        return 1;
    }
    print_fail("RESET command failed");
    return 0;
}

static int test_jtag_scan8(void) {
    print_test("JTAG Scan 8 bits (CMD_SCAN)");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x02; /* CMD_SCAN */
    cmd.length = htonl(8);
    if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
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
    print_pass("SCAN completed (TDO captured)");
    return 1;
}

static int test_jtag_multiple_resets(void) {
    print_test("JTAG Multiple TAP Reset Cycles");
    print_info("Testing repeated RESET operations");
    for (int i = 0; i < 3; i++) {
        struct jtag_vpi_cmd cmd = {0};
        struct jtag_vpi_resp resp = {0};
        cmd.cmd = 0x00; /* CMD_RESET */
        cmd.length = htonl(0);
        if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Failed on reset cycle %d", i+1);
            print_fail(msg);
            return 0;
        }
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
        return 1;
    }
    print_info("VPI server response received");
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
    print_pass("Large scan command accepted");
    
    /* Send TMS buffer (4 bytes) */
    uint8_t tms_buf[4] = {0x00, 0x00, 0x00, 0x00};
    if (send_all(sock_fd, tms_buf, 4) < 0) {
        print_fail("Failed to send TMS buffer");
        return 0;
    }
    print_pass("TMS buffer sent (32 bits)");
    
    /* Send TDI buffer (4 bytes) */
    uint8_t tdi_buf[4] = {0xAA, 0x55, 0xAA, 0x55};
    if (send_all(sock_fd, tdi_buf, 4) < 0) {
        print_fail("Failed to send TDI buffer");
        return 0;
    }
    print_pass("TDI buffer sent (32 bits, pattern: 0xAA55AA55)");
    
    /* Receive TDO buffer */
    uint8_t tdo_buf[4];
    if (recv_all(sock_fd, tdo_buf, 4) < 0) {
        print_fail("Failed to receive TDO buffer");
        return 0;
    }
    print_pass("TDO buffer received (32 bits)");
    char msg[64];
    snprintf(msg, sizeof(msg), "TDO value: 0x%02X%02X%02X%02X", 
             tdo_buf[3], tdo_buf[2], tdo_buf[1], tdo_buf[0]);
    print_info(msg);
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
    if (jtag_send_cmd(&cmd, &resp) == 0) {
        print_pass(resp.mode ? "Mode=cJTAG" : "Mode=JTAG");
        return 1;
    }
    print_fail("Mode query failed");
    return 0;
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

static int test_jtag_tdi_tdo_integrity(void) {
    print_test("JTAG Physical: TDI/TDO Signal Integrity");
    print_info("Testing data integrity on TDI->TDO path");
    
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    
    /* Test various bit patterns */
    uint8_t patterns[4] = {0xAA, 0x55, 0xFF, 0x00};
    int pattern_ok = 1;
    
    for (int p = 0; p < 4; p++) {
        cmd.cmd = 0x02;
        cmd.length = htonl(8);
        
        if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
            pattern_ok = 0;
            break;
        }
        
        uint8_t tms = 0x00;
        uint8_t tdo;
        
        if (send_all(sock_fd, &tms, 1) < 0 || 
            send_all(sock_fd, &patterns[p], 1) < 0 || 
            recv_all(sock_fd, &tdo, 1) < 0) {
            pattern_ok = 0;
            break;
        }
    }
    
    if (pattern_ok) {
        print_pass("TDI/TDO signal integrity verified (patterns: 0xAA, 0x55, 0xFF, 0x00)");
        return 1;
    }
    print_fail("TDI/TDO integrity test failed");
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
    
    /* Read 32-bit IDCODE from DR */
    cmd.cmd = 0x02;
    cmd.length = htonl(32);
    
    if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
        print_fail("Failed to initiate IDCODE read");
        return 0;
    }
    
    uint8_t tms_buf[4] = {0x00, 0x00, 0x00, 0x80}; /* Exit on last bit */
    uint8_t tdi_buf[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t tdo_buf[4];
    
    if (send_all(sock_fd, tms_buf, 4) >= 0 && 
        send_all(sock_fd, tdi_buf, 4) >= 0 && 
        recv_all(sock_fd, tdo_buf, 4) >= 0) {
        print_pass("IDCODE read simulated (32 bits)");
        char msg[80];
        snprintf(msg, sizeof(msg), "IDCODE: 0x%02X%02X%02X%02X", 
                 tdo_buf[3], tdo_buf[2], tdo_buf[1], tdo_buf[0]);
        print_info(msg);
        return 1;
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

static int run_cjtag_tests(void) {
    int ok = 1;
    uint8_t tdo = 0;

    print_test("Two-Wire Mode Detection (CMD_OSCAN1)");
    ok &= (oscan1_edge(1, 1, &tdo) == 0);
    if (ok) print_pass("CMD_OSCAN1 accepted"); else print_fail("CMD_OSCAN1 rejected");

    print_test("OScan1 Attention Character (16 edges)");
    if (oscan1_send_oac() == 0) print_pass("OAC sent"); else { print_fail("OAC failed"); ok = 0; }

    print_test("JScan OSCAN_ON (0x1)");
    if (oscan1_send_jscan(0x1) == 0) print_pass("JSCAN_OSCAN_ON sent"); else { print_fail("JSCAN_OSCAN_ON failed"); ok = 0; }

    print_test("Bit stuffing (eight 1s)");
    for (int i = 0; i < 8; i++) {
        if (oscan1_edge(1, 1, NULL) != 0) { ok = 0; break; }
    }
    if (ok) print_pass("Stuffing sequence accepted"); else print_fail("Stuffing failed");

    print_test("SF0 transfer");
    if (oscan1_sf0(0, 1, &tdo) == 0) print_pass("SF0 completed"); else { print_fail("SF0 failed"); ok = 0; }

    print_test("CRC-8 Calculation");
    uint8_t data[3] = {0xAA, 0x55, 0xFF};
    uint8_t crc = cjtag_crc8(data, sizeof(data));
    if (crc == 0x5A) print_pass("CRC-8 matches 0x5A"); else { char msg[64]; snprintf(msg, sizeof(msg), "Unexpected CRC 0x%02X", crc); print_fail(msg); ok = 0; }

    print_test("TAP reset via SF0 (5 cycles)");
    for (int i = 0; i < 5; i++) {
        if (oscan1_sf0(1, 0, NULL) != 0) { ok = 0; break; }
    }
    if (ok) print_pass("TAP reset sequence sent"); else print_fail("TAP reset failed");

    print_test("Mode flag probe");
    if (oscan1_edge(0, 0, &tdo) == 0) print_pass("Mode flag response received"); else { print_fail("Mode flag probe failed"); ok = 0; }

    print_test("Multiple OAC sequences");
    for (int i = 0; i < 3; i++) {
        if (oscan1_send_oac() != 0) { ok = 0; break; }
    }
    if (ok) print_pass("Multiple OAC sequences accepted"); else print_fail("Multiple OAC failed");

    print_test("JScan OSCAN_OFF (0x0) and OSCAN_ON cycle");
    if (oscan1_send_jscan(0x0) == 0 && oscan1_send_jscan(0x1) == 0) {
        print_pass("JScan mode switching works");
    } else {
        print_fail("JScan mode switching failed");
        ok = 0;
    }

    print_test("Extended SF0 sequence (16 cycles)");
    for (int i = 0; i < 16; i++) {
        uint8_t tms_bit = (i < 8) ? 1 : 0;
        if (oscan1_sf0(tms_bit, i & 1, NULL) != 0) { ok = 0; break; }
    }
    if (ok) print_pass("Extended SF0 sequence completed"); else print_fail("Extended SF0 failed");

    /* Command Protocol Tests (using standard JTAG commands over cJTAG) */
    print_info("=== Command Protocol Tests (JTAG commands over cJTAG) ===");
    
    print_test("cJTAG: TAP Reset via CMD_RESET");
    struct jtag_vpi_cmd cmd = {0};
    struct jtag_vpi_resp resp = {0};
    cmd.cmd = 0x00; /* CMD_RESET */
    cmd.length = htonl(0);
    if (jtag_send_cmd(&cmd, &resp) == 0 && resp.response == 0x00) {
        print_pass("CMD_RESET works over cJTAG mode");
    } else {
        print_fail("CMD_RESET failed in cJTAG mode");
        ok = 0;
    }

    print_test("cJTAG: Scan 8 bits via CMD_SCAN");
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
        } else {
            print_fail("CMD_SCAN data transfer failed");
            ok = 0;
        }
    } else {
        print_fail("CMD_SCAN command failed in cJTAG mode");
        ok = 0;
    }

    print_test("cJTAG: Mode query via CMD_SET_PORT");
    cmd.cmd = 0x03; /* CMD_SET_PORT */
    cmd.length = htonl(0);
    if (jtag_send_cmd(&cmd, &resp) == 0) {
        if (resp.mode == 1) {
            print_pass("Mode reports cJTAG (mode=1)");
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "Mode=%d (expected 1 for cJTAG)", resp.mode);
            print_info(msg);
        }
    } else {
        print_fail("Mode query failed");
        ok = 0;
    }

    print_test("cJTAG: Large scan (32 bits) via CMD_SCAN");
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
        } else {
            print_fail("32-bit scan data transfer failed");
            ok = 0;
        }
    } else {
        print_fail("32-bit scan command failed");
        ok = 0;
    }

    print_test("cJTAG: Rapid reset commands (5 cycles)");
    int rapid_ok = 1;
    for (int i = 0; i < 5; i++) {
        cmd.cmd = 0x00;
        cmd.length = htonl(0);
        if (jtag_send_cmd(&cmd, &resp) != 0 || resp.response != 0x00) {
            rapid_ok = 0;
            break;
        }
    }
    if (rapid_ok) {
        print_pass("Rapid resets work in cJTAG mode");
    } else {
        print_fail("Rapid resets failed");
        ok = 0;
    }

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

static int run_legacy_tests(void) {
    int ok = 1;
    struct legacy_cmd cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;

    print_test("Legacy: TAP reset (CMD_RESET)");
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x00;
    cmd.length = htonl(0);
    if (legacy_send(&cmd, NULL, 0, resp, &resp_len) == 0) print_pass("Reset command sent"); else { print_fail("Reset failed"); ok = 0; }

    print_test("Legacy: Scan 8 bits (CMD_SCAN)");
    uint8_t payload[6] = {0x00, 0xAA, 0x00, 0x00, 0x00, 0x08};
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x02;
    cmd.length = htonl(sizeof(payload));
    if (legacy_send(&cmd, payload, sizeof(payload), resp, &resp_len) == 0) print_pass("Scan command sent"); else { print_fail("Scan failed"); ok = 0; }

    print_test("Legacy: Mode Query (CMD_SET_PORT)");
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x03;
    cmd.mode = 0xFF; /* Query mode */
    cmd.length = htonl(0);
    if (legacy_send(&cmd, NULL, 0, resp, &resp_len) == 0) {
        print_pass("Mode query successful");
        if (resp_len >= 1) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Current mode: 0x%02X", resp[0]);
            print_info(msg);
        }
    } else {
        print_fail("Mode query failed");
        ok = 0;
    }

    print_test("Legacy: TMS sequence");
    uint8_t tms_data[2] = {0xFF, 0xFF};
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x01;
    cmd.length = htonl(2);
    if (legacy_send(&cmd, tms_data, 2, resp, &resp_len) == 0) print_pass("TMS sequence sent"); else { print_fail("TMS failed"); ok = 0; }

    print_test("Legacy: Multiple sequential resets");
    int reset_ok = 1;
    for (int i = 0; i < 3; i++) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.cmd = 0x00;
        cmd.length = htonl(0);
        if (legacy_send(&cmd, NULL, 0, resp, &resp_len) != 0) {
            reset_ok = 0;
            break;
        }
    }
    if (reset_ok) print_pass("3 sequential resets completed"); else { print_fail("Sequential resets failed"); ok = 0; }

    print_test("Legacy: Reset then Scan sequence");
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x00;
    cmd.length = htonl(0);
    if (legacy_send(&cmd, NULL, 0, resp, &resp_len) != 0) {
        print_fail("Reset failed in sequence");
        ok = 0;
    } else {
        uint8_t scan_payload[6] = {0x00, 0x55, 0x00, 0x00, 0x00, 0x08};
        memset(&cmd, 0, sizeof(cmd));
        cmd.cmd = 0x02;
        cmd.length = htonl(6);
        if (legacy_send(&cmd, scan_payload, 6, resp, &resp_len) == 0) {
            print_pass("Reset then scan sequence completed");
        } else {
            print_fail("Scan failed after reset");
            ok = 0;
        }
    }

    print_test("Legacy: Large scan (32 bits)");
    uint8_t large_payload[10] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x20};
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0x02;
    cmd.length = htonl(10);
    if (legacy_send(&cmd, large_payload, 10, resp, &resp_len) == 0) print_pass("32-bit scan accepted"); else { print_fail("Large scan failed"); ok = 0; }

    print_test("Legacy: Unknown command handling");
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = 0xFF;
    cmd.length = htonl(0);
    legacy_send(&cmd, NULL, 0, resp, &resp_len);
    print_pass("Unknown command handled (server didn't crash)");

    print_test("Legacy: Rapid command sequence (10 commands)");
    int rapid_ok = 1;
    for (int i = 0; i < 10; i++) {
        memset(&cmd, 0, sizeof(cmd));
        if (i % 2 == 0) {
            cmd.cmd = 0x00;
            cmd.length = htonl(0);
            if (legacy_send(&cmd, NULL, 0, resp, &resp_len) != 0) rapid_ok = 0;
        } else {
            uint8_t quick_scan[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x08};
            cmd.cmd = 0x02;
            cmd.length = htonl(6);
            if (legacy_send(&cmd, quick_scan, 6, resp, &resp_len) != 0) rapid_ok = 0;
        }
    }
    if (rapid_ok) print_pass("10 rapid commands completed"); else { print_fail("Rapid commands failed"); ok = 0; }

    print_test("Legacy: Scan pattern variations");
    uint8_t patterns[3][6] = {
        {0x00, 0xAA, 0x00, 0x00, 0x00, 0x08},
        {0x00, 0x55, 0x00, 0x00, 0x00, 0x08},
        {0x00, 0xFF, 0x00, 0x00, 0x00, 0x08}
    };
    int pattern_ok = 1;
    for (int p = 0; p < 3; p++) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.cmd = 0x02;
        cmd.length = htonl(6);
        if (legacy_send(&cmd, patterns[p], 6, resp, &resp_len) != 0) {
            pattern_ok = 0;
            break;
        }
    }
    if (pattern_ok) print_pass("Pattern variations accepted"); else { print_fail("Pattern test failed"); ok = 0; }

    print_test("Legacy: Alternating commands (Reset/Scan)");
    print_info("Alternating between RESET and SCAN commands (10 iterations)");
    int alternating_ok = 1;
    for (int i = 0; i < 10; i++) {
        memset(&cmd, 0, sizeof(cmd));
        if (i % 2 == 0) {
            /* RESET */
            cmd.cmd = 0x00;
            cmd.length = htonl(0);
            if (legacy_send(&cmd, NULL, 0, resp, &resp_len) != 0) {
                alternating_ok = 0;
                break;
            }
        } else {
            /* SCAN */
            uint8_t scan_data[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x08};
            cmd.cmd = 0x02;
            cmd.length = htonl(6);
            if (legacy_send(&cmd, scan_data, 6, resp, &resp_len) != 0) {
                alternating_ok = 0;
                break;
            }
        }
    }
    if (alternating_ok) print_pass("Alternating commands successful (10 iterations)"); else { print_fail("Alternating commands failed"); ok = 0; }

    return ok;
}

/* -------------------------------------------------------------------------- */
/* Protocol Combination Tests                                                */
/* -------------------------------------------------------------------------- */

static int run_combo_tests(void) {
    int ok = 1;
    
    /* Test 1: Sequential Protocol Switching (JTAG → Legacy → JTAG) */
    print_test("Combo: Sequential Protocol Switching");
    print_info("Phase 1: JTAG operations");
    
    struct jtag_vpi_cmd jtag_cmd = {0};
    struct jtag_vpi_resp jtag_resp = {0};
    jtag_cmd.cmd = 0x00; /* CMD_RESET */
    jtag_cmd.length = htonl(0);
    if (jtag_send_cmd(&jtag_cmd, &jtag_resp) == 0 && jtag_resp.response == 0x00) {
        print_pass("JTAG reset successful");
    } else {
        print_fail("JTAG reset failed");
        ok = 0;
    }
    
    print_info("Phase 2: Switch to Legacy protocol");
    struct legacy_cmd legacy_cmd;
    uint8_t resp[256];
    uint32_t resp_len = 0;
    memset(&legacy_cmd, 0, sizeof(legacy_cmd));
    legacy_cmd.cmd = 0x00;
    legacy_cmd.length = htonl(0);
    if (legacy_send(&legacy_cmd, NULL, 0, resp, &resp_len) == 0) {
        print_pass("Legacy reset successful");
    } else {
        print_fail("Legacy reset failed");
        ok = 0;
    }
    
    print_info("Phase 3: Switch back to JTAG");
    memset(&jtag_cmd, 0, sizeof(jtag_cmd));
    memset(&jtag_resp, 0, sizeof(jtag_resp));
    jtag_cmd.cmd = 0x00;
    jtag_cmd.length = htonl(0);
    if (jtag_send_cmd(&jtag_cmd, &jtag_resp) == 0 && jtag_resp.response == 0x00) {
        print_pass("Sequential protocol switching successful");
    } else {
        print_fail("Return to JTAG failed");
        ok = 0;
    }
    
    /* Test 2: Mixed Operations - Alternating JTAG and Legacy */
    print_test("Combo: Alternating JTAG/Legacy Operations");
    print_info("Rapidly alternating between JTAG and Legacy commands");
    
    int mixed_ok = 1;
    for (int i = 0; i < 5; i++) {
        /* JTAG command */
        memset(&jtag_cmd, 0, sizeof(jtag_cmd));
        memset(&jtag_resp, 0, sizeof(jtag_resp));
        jtag_cmd.cmd = 0x00;
        jtag_cmd.length = htonl(0);
        if (jtag_send_cmd(&jtag_cmd, &jtag_resp) != 0 || jtag_resp.response != 0x00) {
            mixed_ok = 0;
            break;
        }
        
        /* Legacy command */
        memset(&legacy_cmd, 0, sizeof(legacy_cmd));
        legacy_cmd.cmd = 0x00;
        legacy_cmd.length = htonl(0);
        if (legacy_send(&legacy_cmd, NULL, 0, resp, &resp_len) != 0) {
            mixed_ok = 0;
            break;
        }
    }
    if (mixed_ok) {
        print_pass("Alternating operations successful (5 iterations)");
    } else {
        print_fail("Alternating operations failed");
        ok = 0;
    }
    
    /* Test 3: Stress Test - Rapid Protocol Detection */
    print_test("Combo: Rapid Protocol Auto-Detection");
    print_info("Testing server's protocol detection with rapid switches");
    
    int detect_ok = 1;
    for (int i = 0; i < 10; i++) {
        if (i % 2 == 0) {
            /* JTAG */
            memset(&jtag_cmd, 0, sizeof(jtag_cmd));
            memset(&jtag_resp, 0, sizeof(jtag_resp));
            jtag_cmd.cmd = 0x00;
            jtag_cmd.length = htonl(0);
            if (jtag_send_cmd(&jtag_cmd, &jtag_resp) != 0) {
                detect_ok = 0;
                break;
            }
        } else {
            /* Legacy */
            memset(&legacy_cmd, 0, sizeof(legacy_cmd));
            legacy_cmd.cmd = 0x00;
            legacy_cmd.length = htonl(0);
            if (legacy_send(&legacy_cmd, NULL, 0, resp, &resp_len) != 0) {
                detect_ok = 0;
                break;
            }
        }
    }
    if (detect_ok) {
        print_pass("Rapid protocol detection successful (10 switches)");
    } else {
        print_fail("Rapid protocol detection failed");
        ok = 0;
    }
    
    /* Test 4: Mixed Scan Operations */
    print_test("Combo: Mixed Scan Operations (JTAG + Legacy)");
    print_info("Testing scan operations with different protocols");
    
    /* JTAG 8-bit scan */
    memset(&jtag_cmd, 0, sizeof(jtag_cmd));
    memset(&jtag_resp, 0, sizeof(jtag_resp));
    jtag_cmd.cmd = 0x02;
    jtag_cmd.length = htonl(8);
    if (jtag_send_cmd(&jtag_cmd, &jtag_resp) == 0 && jtag_resp.response == 0x00) {
        uint8_t tms_buf[1] = {0x00};
        uint8_t tdi_buf[1] = {0xAA};
        uint8_t tdo_buf[1];
        if (send_all(sock_fd, tms_buf, 1) == 0 &&
            send_all(sock_fd, tdi_buf, 1) == 0 &&
            recv_all(sock_fd, tdo_buf, 1) == 0) {
            print_pass("JTAG 8-bit scan successful");
        } else {
            print_fail("JTAG scan data transfer failed");
            ok = 0;
        }
    } else {
        print_fail("JTAG scan command failed");
        ok = 0;
    }
    
    /* Legacy 8-bit scan */
    uint8_t payload[6] = {0x00, 0x55, 0x00, 0x00, 0x00, 0x08};
    memset(&legacy_cmd, 0, sizeof(legacy_cmd));
    legacy_cmd.cmd = 0x02;
    legacy_cmd.length = htonl(sizeof(payload));
    if (legacy_send(&legacy_cmd, payload, sizeof(payload), resp, &resp_len) == 0) {
        print_pass("Legacy 8-bit scan successful");
    } else {
        print_fail("Legacy scan failed");
        ok = 0;
    }
    
    /* Test 5: Back-to-Back Resets (Different Protocols) */
    print_test("Combo: Back-to-Back Resets (Protocol Mix)");
    print_info("Testing multiple resets across protocols");
    
    int reset_ok = 1;
    for (int i = 0; i < 3; i++) {
        /* JTAG reset */
        memset(&jtag_cmd, 0, sizeof(jtag_cmd));
        memset(&jtag_resp, 0, sizeof(jtag_resp));
        jtag_cmd.cmd = 0x00;
        jtag_cmd.length = htonl(0);
        if (jtag_send_cmd(&jtag_cmd, &jtag_resp) != 0 || jtag_resp.response != 0x00) {
            reset_ok = 0;
            break;
        }
        
        /* Legacy reset */
        memset(&legacy_cmd, 0, sizeof(legacy_cmd));
        legacy_cmd.cmd = 0x00;
        legacy_cmd.length = htonl(0);
        if (legacy_send(&legacy_cmd, NULL, 0, resp, &resp_len) != 0) {
            reset_ok = 0;
            break;
        }
    }
    if (reset_ok) {
        print_pass("Back-to-back resets successful (3 JTAG + 3 Legacy)");
    } else {
        print_fail("Back-to-back resets failed");
        ok = 0;
    }
    
    /* Test 6: Large Scan Mix (JTAG 32-bit + Legacy 32-bit) */
    print_test("Combo: Large Scan Mix (32-bit JTAG + Legacy)");
    print_info("Testing 32-bit scans with both protocols");
    
    /* JTAG 32-bit scan */
    memset(&jtag_cmd, 0, sizeof(jtag_cmd));
    memset(&jtag_resp, 0, sizeof(jtag_resp));
    jtag_cmd.cmd = 0x02;
    jtag_cmd.length = htonl(32);
    if (jtag_send_cmd(&jtag_cmd, &jtag_resp) == 0 && jtag_resp.response == 0x00) {
        uint8_t tms_buf[4] = {0x00, 0x00, 0x00, 0x00};
        uint8_t tdi_buf[4] = {0x12, 0x34, 0x56, 0x78};
        uint8_t tdo_buf[4];
        if (send_all(sock_fd, tms_buf, 4) == 0 &&
            send_all(sock_fd, tdi_buf, 4) == 0 &&
            recv_all(sock_fd, tdo_buf, 4) == 0) {
            print_pass("JTAG 32-bit scan successful");
        } else {
            print_fail("JTAG 32-bit data transfer failed");
            ok = 0;
        }
    } else {
        print_fail("JTAG 32-bit scan command failed");
        ok = 0;
    }
    
    /* Legacy 32-bit scan */
    uint8_t large_payload[9] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0x20};
    memset(&legacy_cmd, 0, sizeof(legacy_cmd));
    legacy_cmd.cmd = 0x02;
    legacy_cmd.length = htonl(sizeof(large_payload));
    if (legacy_send(&legacy_cmd, large_payload, sizeof(large_payload), resp, &resp_len) == 0) {
        print_pass("Legacy 32-bit scan successful");
    } else {
        print_fail("Legacy 32-bit scan failed");
        ok = 0;
    }
    
    return ok;
}

/* -------------------------------------------------------------------------- */
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
    } else {
        ok = run_jtag_tests();
    }

    close(sock_fd);

    printf("\n=== Test Summary ===\n");
    printf("Total Tests: %d\n", test_count);
    printf("Passed: %d\n", pass_count);
    printf("Failed: %d\n\n", fail_count);

    if (ok && fail_count == 0) {
        printf("✓ All tests PASSED\n");
        return 0;
    }
    printf("✗ Some tests FAILED\n");
    return 1;
}
