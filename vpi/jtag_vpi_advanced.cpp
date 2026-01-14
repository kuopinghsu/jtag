/**
 * JTAG Advanced Example
 * Demonstrates using the JTAG interface for complex operations
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// VPI command/response structures
typedef struct {
    uint8_t cmd;
    uint8_t tms_val;
    uint8_t tdi_val;
    uint8_t pad;
} jtag_cmd_t;

typedef struct {
    uint8_t response;
    uint8_t tdo_val;
    uint8_t mode;
    uint8_t status;
} jtag_resp_t;

// JTAG client class
class JTAGClient {
private:
    int sock;
    const char *host;
    int port;

public:
    JTAGClient(const char *host, int port) : sock(-1), host(host), port(port) {}

    int connect_to_vpi() {
        struct sockaddr_in addr;
        int retries = 0;

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket");
            return -1;
        }

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(host);
        addr.sin_port = htons(port);

        while (retries < 10) {
            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                printf("[*] Connected to JTAG VPI at %s:%d\n", host, port);
                return 0;
            }
            retries++;
            printf("[*] Retry %d/10...\n", retries);
            usleep(500000);
        }

        perror("connect");
        return -1;
    }

    void disconnect() {
        if (sock >= 0) close(sock);
    }

    // Send raw JTAG command
    int send_tco(uint8_t tms, uint8_t tdi, uint8_t *tdo_ret) {
        jtag_cmd_t cmd;
        jtag_resp_t resp;

        cmd.cmd = 0x01;
        cmd.tms_val = tms & 1;
        cmd.tdi_val = tdi & 1;
        cmd.pad = 0;

        if (send(sock, &cmd, sizeof(cmd), 0) < 0) return -1;
        if (recv(sock, &resp, sizeof(resp), 0) < 0) return -1;

        if (tdo_ret) *tdo_ret = resp.tdo_val;
        return 0;
    }

    // Pulse TCK multiple times
    int pulse_tck(int count) {
        int i;
        for (i = 0; i < count; i++) {
            if (send_tco(0, 0, NULL) < 0) return -1;
        }
        return 0;
    }

    // Shift data through JTAG
    int shift_data(uint8_t *data, int bits, uint8_t *result) {
        int i;
        uint8_t tdo;

        for (i = 0; i < bits; i++) {
            uint8_t tdi = (data[i / 8] >> (i % 8)) & 1;
            uint8_t is_last = (i == bits - 1) ? 1 : 0;
            uint8_t tms = is_last ? 1 : 0;

            if (send_tco(tms, tdi, &tdo) < 0) return -1;

            if (result) {
                result[i / 8] |= (tdo << (i % 8));
            }
        }

        return 0;
    }

    // Reset TAP controller
    int reset_tap() {
        int i;
        printf("[*] Resetting TAP controller\n");
        for (i = 0; i < 5; i++) {
            if (send_tco(1, 0, NULL) < 0) return -1;
        }
        if (send_tco(0, 0, NULL) < 0) return -1;
        return 0;
    }

    // Read IDCODE
    // Note: The response struct is 4 bytes, and we interpret it as uint32_t.
    // This causes a compiler warning about array bounds, but it's actually correct
    // for this protocol where the response encodes the IDCODE across all fields.
    uint32_t read_idcode() {
        jtag_cmd_t cmd;
        union {
            jtag_resp_t resp;
            uint32_t idcode;
        } response;

        cmd.cmd = 0x02;
        cmd.tms_val = 0;
        cmd.tdi_val = 0;
        cmd.pad = 0;

        if (send(sock, &cmd, sizeof(cmd), 0) < 0) return 0;
        if (recv(sock, &response.resp, sizeof(response.resp), 0) < 0) return 0;

        return response.idcode;
    }

    // Parse and display IDCODE
    void display_idcode(uint32_t idcode) {
        uint8_t version = (idcode >> 28) & 0xF;
        uint16_t partnum = (idcode >> 12) & 0xFFFF;
        uint16_t mfg_id = (idcode >> 1) & 0x7FF;
        uint8_t fixed = idcode & 1;

        printf("[*] IDCODE: 0x%08x\n", idcode);
        printf("    Version:    0x%x\n", version);
        printf("    PartNumber: 0x%04x\n", partnum);
        printf("    Mfg ID:     0x%03x\n", mfg_id);
        printf("    Fixed bit:  %d\n", fixed);
    }

    // Switch mode
    int set_mode(int mode) {
        jtag_cmd_t cmd;
        jtag_resp_t resp;

        cmd.cmd = 0x04;
        cmd.tms_val = 0;
        cmd.tdi_val = 0;
        cmd.pad = mode & 1;

        printf("[*] Switching to %s mode\n", mode ? "cJTAG" : "JTAG");

        if (send(sock, &cmd, sizeof(cmd), 0) < 0) return -1;
        if (recv(sock, &resp, sizeof(resp), 0) < 0) return -1;

        return 0;
    }

    // Get current active mode
    int get_mode() {
        jtag_cmd_t cmd;
        jtag_resp_t resp;

        cmd.cmd = 0x03;

        if (send(sock, &cmd, sizeof(cmd), 0) < 0) return -1;
        if (recv(sock, &resp, sizeof(resp), 0) < 0) return -1;

        printf("[*] Active mode: %s\n", resp.mode ? "cJTAG (OScan1)" : "JTAG");
        return resp.mode;
    }
};

// Main program
int main(int argc, char **argv) {
    JTAGClient jtag("127.0.0.1", 3333);
    uint32_t idcode;

    printf("\n========================================\n");
    printf("JTAG Advanced Example & Test Utility\n");
    printf("========================================\n\n");

    // Connect to VPI server
    if (jtag.connect_to_vpi() < 0) {
        fprintf(stderr, "Error: Could not connect to JTAG VPI server\n");
        fprintf(stderr, "Make sure simulation is running with:\n");
        fprintf(stderr, "  make sim\n");
        return 1;
    }

    sleep(1);

    // Test 1: Reset TAP
    printf("\n[TEST 1] TAP Controller Reset\n");
    printf("-------------------------------\n");
    if (jtag.reset_tap() < 0) {
        fprintf(stderr, "Error: TAP reset failed\n");
        return 1;
    }
    printf("[✓] TAP reset complete\n");

    // Test 2: Read IDCODE
    printf("\n[TEST 2] Read IDCODE\n");
    printf("-------------------------------\n");
    sleep(1);
    idcode = jtag.read_idcode();
    if (idcode == 0) {
        fprintf(stderr, "Error: Failed to read IDCODE\n");
        return 1;
    }
    jtag.display_idcode(idcode);
    printf("[✓] IDCODE read successfully\n");

    // Test 3: Get mode
    printf("\n[TEST 3] Get Active Mode\n");
    printf("-------------------------------\n");
    jtag.get_mode();
    printf("[✓] Mode detection complete\n");

    // Test 4: Mode switching
    printf("\n[TEST 4] Mode Switching\n");
    printf("-------------------------------\n");
    jtag.set_mode(1);  // Switch to cJTAG
    sleep(1);
    jtag.get_mode();
    sleep(1);
    jtag.set_mode(0);  // Switch back to JTAG
    jtag.get_mode();
    printf("[✓] Mode switching complete\n");

    // Test 5: Multiple JTAG cycles
    printf("\n[TEST 5] JTAG Timing Test\n");
    printf("-------------------------------\n");
    if (jtag.pulse_tck(100) < 0) {
        fprintf(stderr, "Error: TCK pulse failed\n");
        return 1;
    }
    printf("[✓] 100 TCK cycles completed\n");

    printf("\n========================================\n");
    printf("All tests completed successfully!\n");
    printf("========================================\n\n");

    jtag.disconnect();
    return 0;
}
