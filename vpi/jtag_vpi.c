/**
 * JTAG VPI Interface
 * Allows external tools (like OpenOCD) to control JTAG through VPI
 *
 * This C++ module interfaces between the simulation and external JTAG controllers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "vpi_user.h"

// Enable verbose logging (set to 1 for detailed trace)
#define VPI_VERBOSE 1

#if VPI_VERBOSE
#define VPI_TRACE(...) vpi_printf(__VA_ARGS__)
#else
#define VPI_TRACE(...) do {} while(0)
#endif

// VPI handles for JTAG signals
static vpiHandle tck_h, tms_h, tdi_h, tdo_h, trst_n_h, mode_select_h;
static vpiHandle tco_h;
static vpiHandle clk_h, rst_n_h;
static vpiHandle idcode_h, debug_req_h, active_mode_h;

// Socket for communication
static int server_sock = -1;
static int client_sock = -1;
static pthread_t server_thread;

// VPI command structure
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

/**
 * Read a signal value from simulation
 */
static unsigned int read_signal(vpiHandle handle) {
    s_vpi_value value;
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    VPI_TRACE("[VPI_TRACE] Read signal: 0x%x\n", value.value.integer);
    return value.value.integer;
}

/**
 * Write a signal value to simulation
 */
static void write_signal(vpiHandle handle, unsigned int val) {
    s_vpi_value value;
    value.format = vpiIntVal;
    value.value.integer = val;
    VPI_TRACE("[VPI_TRACE] Write signal: 0x%x\n", val);
    vpi_put_value(handle, &value, NULL, vpiNoDelay);
}

/**
 * JTAG clock pulse
 */
static void pulse_tck(void) {
    write_signal(tck_h, 1);
    write_signal(tck_h, 0);
}

/**
 * Process incoming VPI command from external tool
 */
static void process_vpi_command(jtag_cmd_t *cmd, jtag_resp_t *resp) {
    unsigned int tdo_val;
    unsigned int idcode;

    VPI_TRACE("[VPI_TRACE] Received command: cmd=0x%02x, tms=0x%02x, tdi=0x%02x, pad=0x%02x\n",
              cmd->cmd, cmd->tms_val, cmd->tdi_val, cmd->pad);

    resp->response = 0;

    switch(cmd->cmd) {
        case 0x01:  // Set TMS and TDI, pulse TCK
            VPI_TRACE("[VPI_TRACE] CMD 0x01: Set TMS=%d, TDI=%d, pulse TCK\n",
                      cmd->tms_val & 1, cmd->tdi_val & 1);
            write_signal(tms_h, cmd->tms_val & 1);
            write_signal(tdi_h, cmd->tdi_val & 1);
            pulse_tck();
            tdo_val = read_signal(tdo_h);
            resp->tdo_val = tdo_val & 1;
            resp->response = 0x01;  // ACK
            VPI_TRACE("[VPI_TRACE] CMD 0x01: TDO=%d\n", resp->tdo_val);
            break;

        case 0x02:  // Read IDCODE
            VPI_TRACE("[VPI_TRACE] CMD 0x02: Read IDCODE\n");
            idcode = read_signal(idcode_h);
            resp->response = 0x02;
            *(unsigned int*)&resp->status = idcode;
            VPI_TRACE("[VPI_TRACE] CMD 0x02: IDCODE=0x%08x\n", idcode);
            break;

        case 0x03:  // Get active mode
            VPI_TRACE("[VPI_TRACE] CMD 0x03: Get active mode\n");
            resp->mode = read_signal(active_mode_h) & 1;
            resp->response = 0x03;
            VPI_TRACE("[VPI_TRACE] CMD 0x03: Mode=%d\n", resp->mode);
            break;

        case 0x04:  // Set mode select
            VPI_TRACE("[VPI_TRACE] CMD 0x04: Set mode_select=%d\n", cmd->pad & 1);
            write_signal(mode_select_h, cmd->pad & 1);
            resp->response = 0x04;
            break;

        case 0x05:  // Get TDO
            VPI_TRACE("[VPI_TRACE] CMD 0x05: Get TDO\n");
            tdo_val = read_signal(tdo_h);
            resp->tdo_val = tdo_val & 1;
            resp->response = 0x05;
            VPI_TRACE("[VPI_TRACE] CMD 0x05: TDO=%d\n", resp->tdo_val);
            break;

        case 0x06:  // Get debug request status
            VPI_TRACE("[VPI_TRACE] CMD 0x06: Get debug_req\n");
            resp->status = read_signal(debug_req_h) & 1;
            resp->response = 0x06;
            VPI_TRACE("[VPI_TRACE] CMD 0x06: debug_req=%d\n", resp->status);
            break;

        default:
            VPI_TRACE("[VPI_TRACE] CMD 0x%02x: UNKNOWN - returning ERROR\n", cmd->cmd);
            resp->response = 0xFF;  // ERROR
            break;
    }

    VPI_TRACE("[VPI_TRACE] Response: resp=0x%02x, tdo=0x%02x, mode=0x%02x, status=0x%02x\n",
              resp->response, resp->tdo_val, resp->mode, resp->status);
}

/**
 * Server thread - listens for external connections
 */
static void* server_thread_func(void *arg) {
    struct sockaddr_in addr;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    jtag_cmd_t cmd;
    jtag_resp_t resp;
    int ret;

    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        vpi_printf("VPI JTAG: Failed to create socket\n");
        return NULL;
    }

    // Set socket options
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(3333);  // JTAG VPI default port

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        vpi_printf("VPI JTAG: Failed to bind socket\n");
        return NULL;
    }

    // Listen
    listen(server_sock, 1);
    vpi_printf("VPI JTAG Server listening on port 3333\n");

    while (1) {
        // Accept connection
        client_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);

        if (client_sock < 0) {
            continue;
        }

        vpi_printf("VPI JTAG: Client connected\n");
        VPI_TRACE("[VPI_TRACE] ========== Client Connection Established ==========\n");

        // Process commands
        while (1) {
            ret = recv(client_sock, &cmd, sizeof(cmd), 0);

            if (ret <= 0) {
                close(client_sock);
                client_sock = -1;
                vpi_printf("VPI JTAG: Client disconnected\n");
                VPI_TRACE("[VPI_TRACE] ========== Client Connection Closed ==========\n");
                break;
            }

            VPI_TRACE("[VPI_TRACE] Received %d bytes from client\n", ret);

            process_vpi_command(&cmd, &resp);

            ret = send(client_sock, &resp, sizeof(resp), 0);
            if (ret < 0) {
                VPI_TRACE("[VPI_TRACE] Send failed, closing connection\n");
                break;
            }
            VPI_TRACE("[VPI_TRACE] Sent %d bytes to client\n", ret);
        }
    }

    return NULL;
}

/**
 * VPI initialization
 */
static int jtag_vpi_init(p_cb_data cb_data) {
    vpiHandle mod;

    vpi_printf("\n=== JTAG VPI Interface Initializing ===\n");

    // Get module handle
    mod = vpi_handle(vpiSysTfCall, NULL);
    if (!mod) {
        vpi_printf("Failed to get module handle\n");
        return 0;
    }

    // Get signal handles from top module
    tck_h = vpi_handle_by_name("jtag_tb.dut.tck", NULL);
    tms_h = vpi_handle_by_name("jtag_tb.dut.tms", NULL);
    tdi_h = vpi_handle_by_name("jtag_tb.dut.tdi", NULL);
    tdo_h = vpi_handle_by_name("jtag_tb.dut.tdo", NULL);
    trst_n_h = vpi_handle_by_name("jtag_tb.dut.trst_n", NULL);
    mode_select_h = vpi_handle_by_name("jtag_tb.dut.mode_select", NULL);
    tco_h = vpi_handle_by_name("jtag_tb.dut.tco", NULL);
    clk_h = vpi_handle_by_name("jtag_tb.dut.clk", NULL);
    rst_n_h = vpi_handle_by_name("jtag_tb.dut.rst_n", NULL);
    idcode_h = vpi_handle_by_name("jtag_tb.dut.idcode", NULL);
    debug_req_h = vpi_handle_by_name("jtag_tb.dut.debug_req", NULL);
    active_mode_h = vpi_handle_by_name("jtag_tb.dut.active_mode", NULL);

    if (!tck_h || !tdo_h) {
        vpi_printf("Failed to get signal handles\n");
        return 0;
    }

    vpi_printf("VPI Signal handles obtained successfully\n");

    // Start server thread
    pthread_create(&server_thread, NULL, server_thread_func, NULL);

    vpi_printf("=== JTAG VPI Interface Ready ===\n\n");

    return 1;
}

/**
 * VPI registration
 */
void jtag_vpi_register(void) {
    s_vpi_systf_data tf_data;

    tf_data.type = vpiSysTask;
    tf_data.tfname = "$jtag_vpi_init";
    tf_data.calltf = jtag_vpi_init;
    tf_data.compiletf = NULL;
    tf_data.sizetf = NULL;
    tf_data.user_data = NULL;

    vpi_register_systf(&tf_data);
}

/**
 * VPI startup routine
 */
void (*vlog_startup_routines[])(void) = {
    jtag_vpi_register,
    0
};
