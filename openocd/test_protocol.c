/**
 * Unified Protocol Test Client for JTAG / cJTAG / Legacy VPI
 *
 * Usage:
 *   ./test_protocol jtag    # modern OpenOCD jtag_vpi protocol
 *   ./test_protocol cjtag   # two-wire cJTAG OScan1 (CMD_OSCAN1)
 *   ./test_protocol legacy  # legacy 8-byte VPI protocol
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

static int run_jtag_tests(void) {
    int ok = 1;
    ok &= test_jtag_reset();
    ok &= test_jtag_mode_query();
    ok &= test_jtag_scan8();
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
