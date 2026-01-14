/**
 * Simple OpenOCD-compatible client example
 * Connects to JTAG VPI server and performs basic operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct {
    unsigned char cmd;
    unsigned char tms_val;
    unsigned char tdi_val;
    unsigned char pad;
} jtag_cmd_t;

typedef struct {
    unsigned char response;
    unsigned char tdo_val;
    unsigned char mode;
    unsigned char status;
} jtag_resp_t;

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 3333

static int sock = -1;

/**
 * Connect to JTAG VPI server
 */
int jtag_vpi_connect(const char *ip, int port) {
    struct sockaddr_in addr;
    int retries = 0;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);

    // Retry connection a few times
    while (retries < 10) {
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            printf("Connected to JTAG VPI server at %s:%d\n", ip, port);
            return 0;
        }
        retries++;
        usleep(500000);  // 500ms
    }

    perror("connect");
    return -1;
}

/**
 * Send JTAG command
 */
int jtag_vpi_send_cmd(unsigned char cmd, unsigned char tms, unsigned char tdi, unsigned char *tdo) {
    jtag_cmd_t cmd_pkt;
    jtag_resp_t resp;

    cmd_pkt.cmd = cmd;
    cmd_pkt.tms_val = tms;
    cmd_pkt.tdi_val = tdi;
    cmd_pkt.pad = 0;

    if (send(sock, &cmd_pkt, sizeof(cmd_pkt), 0) < 0) {
        perror("send");
        return -1;
    }

    if (recv(sock, &resp, sizeof(resp), 0) < 0) {
        perror("recv");
        return -1;
    }

    if (tdo) {
        *tdo = resp.tdo_val;
    }

    return resp.response;
}

/**
 * Read IDCODE
 * Note: The response struct is 4 bytes, and we interpret it as uint32_t.
 * This causes a compiler warning about array bounds, but it's actually correct
 * for this protocol where the response encodes the IDCODE across all fields.
 */
unsigned int jtag_read_idcode(void) {
    jtag_cmd_t cmd_pkt;
    union {
        jtag_resp_t resp;
        unsigned int idcode;
    } response;

    cmd_pkt.cmd = 0x02;  // Read IDCODE
    cmd_pkt.tms_val = 0;
    cmd_pkt.tdi_val = 0;
    cmd_pkt.pad = 0;

    if (send(sock, &cmd_pkt, sizeof(cmd_pkt), 0) < 0) {
        perror("send");
        return 0;
    }

    if (recv(sock, &response.resp, sizeof(response.resp), 0) < 0) {
        perror("recv");
        return 0;
    }

    return response.idcode;
}

/**
 * Main function - example OpenOCD-like operations
 */
int main(void) {
    unsigned char tdo;
    unsigned int idcode;
    int i;

    printf("JTAG VPI Client - OpenOCD-Compatible\n");
    printf("=====================================\n\n");

    // Connect to server
    if (jtag_vpi_connect(SERVER_IP, SERVER_PORT) < 0) {
        fprintf(stderr, "Failed to connect to JTAG VPI server\n");
        fprintf(stderr, "Make sure simulation is running with VPI support\n");
        return 1;
    }

    // Wait a bit for simulation to settle
    sleep(1);

    // Test 1: Reset TAP controller
    printf("\n[1] Resetting TAP controller...\n");
    for (i = 0; i < 5; i++) {
        (void)jtag_vpi_send_cmd(0x01, 1, 0, &tdo);
        printf("  Pulse %d: TMS=1, TDO=%d\n", i+1, tdo);
    }
    (void)jtag_vpi_send_cmd(0x01, 0, 0, &tdo);
    printf("  Final: TMS=0\n");

    // Test 2: Read IDCODE
    printf("\n[2] Reading IDCODE...\n");
    sleep(1);
    idcode = jtag_read_idcode();
    printf("  IDCODE: 0x%08x\n", idcode);
    printf("  Version: 0x%x\n", (idcode >> 28) & 0xF);
    printf("  PartNumber: 0x%x\n", (idcode >> 12) & 0xFFFF);
    printf("  Manufacturer: 0x%x\n", (idcode >> 1) & 0x7FF);

    // Test 3: Get active mode
    printf("\n[3] Checking active mode...\n");
    jtag_cmd_t cmd_pkt;
    jtag_resp_t resp;
    cmd_pkt.cmd = 0x03;
    send(sock, &cmd_pkt, sizeof(cmd_pkt), 0);
    recv(sock, &resp, sizeof(resp), 0);
    printf("  Active mode: %s\n", resp.mode ? "cJTAG (OScan1)" : "JTAG");

    printf("\n[*] Test completed\n");

    close(sock);
    return 0;
}
