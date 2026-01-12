/**
 * JTAG VPI Server Header
 */

#ifndef JTAG_VPI_SERVER_H
#define JTAG_VPI_SERVER_H

#include <stdint.h>

class JtagVpiServer {
public:
    // Protocol modes (public for client access)
    enum ProtocolMode {
        PROTO_UNKNOWN,
        PROTO_OPENOCD_VPI,
        PROTO_LEGACY_8BYTE,
    };

    JtagVpiServer(int port = 3333);
    ~JtagVpiServer();

    bool init();
    void poll();
    void update_signals(uint8_t tdo, uint32_t idcode, uint8_t mode);
    void update_signals(uint8_t tdo, uint8_t tdo_en, uint32_t idcode, uint8_t mode);
    bool get_pending_signals(uint8_t* tms, uint8_t* tdi, uint8_t* mode_sel, bool* tck_pulse, bool* tckc_toggle = nullptr);
    bool is_client_connected() const { return client_sock >= 0; }
    void set_msb_first(bool v) { msb_first = v; }
    void set_protocol_mode(ProtocolMode m) { protocol_mode = m; }
    void set_debug_level(int level) { debug_level = level; }

private:
    // OpenOCD jtag_vpi protocol (packed) structure size: 1036 bytes
    struct __attribute__((packed)) OcdVpiCmd {
        union { uint32_t cmd; uint8_t cmd_buf[4]; };
        uint8_t buffer_out[512];
        uint8_t buffer_in[512];
        union { uint32_t length; uint8_t length_buf[4]; };
        union { uint32_t nb_bits; uint8_t nb_bits_buf[4]; };
    };

    static constexpr uint32_t VPI_PKT_SIZE = sizeof(OcdVpiCmd);

    // Minimal OpenOCD VPI protocol structures (used by test_protocol)
    struct __attribute__((packed)) MinimalVpiCmd {
        uint8_t cmd;
        uint8_t pad[3];
        uint32_t length;  // little-endian
    };

    struct __attribute__((packed)) MinimalVpiResp {
        uint8_t response;
        uint8_t tdo_val;
        uint8_t mode;
        uint8_t status;
    };

    ProtocolMode protocol_mode = PROTO_UNKNOWN; // start unknown and auto-detect

    int port;
    int server_sock;
    int client_sock;

    // Current signal values
    uint8_t current_tdo;
    uint8_t current_tdo_en;
    uint32_t current_idcode;
    uint8_t current_mode;
    bool msb_first;
    int debug_level;  // 0=off, 1=basic, 2=verbose

    // Pending commands from client
    uint8_t pending_tms;
    uint8_t pending_tdi;
    uint8_t pending_mode_select;
    bool pending_tck_pulse;
    int  reset_pulses_remaining;   // number of TCK pulses to issue for reset

    // cJTAG/OScan1 state
    uint8_t tckc_state;            // Current TCKC level (0 or 1)
    bool pending_tckc_toggle;      // Toggle TCKC for next cycle

    // TCK pulse queue for operations that need multiple cycles (like RESET)
    struct TckOp {
        uint8_t tms;
        uint8_t tdi;
    };
    TckOp tck_queue[16];  // Queue for pending TCK operations
    int tck_queue_head;
    int tck_queue_tail;
    int tck_queue_count;

    // Command receive buffer (handle partial TCP reads)
    uint8_t cmd_buf[8];
    uint32_t cmd_bytes_received;

    // OpenOCD vpi packet receive/send state
    OcdVpiCmd vpi_cmd_rx;
    uint32_t vpi_rx_bytes = 0;
    OcdVpiCmd vpi_cmd_tx;
    uint32_t vpi_tx_bytes = 0;
    bool vpi_tx_pending = false;
    bool vpi_minimal_mode = false;  // true if using 8-byte cmd / 4-byte resp

    // TMS sequence state (OpenOCD)
    bool tms_seq_active = false;
    uint32_t tms_seq_num_bits = 0;
    uint32_t tms_seq_bit_index = 0;
    uint8_t tms_seq_buf[512];

    void enqueue_tck(uint8_t tms, uint8_t tdi);
    bool dequeue_tck(uint8_t* tms, uint8_t* tdi);

    // Scan operation state
    enum ScanState {
        SCAN_IDLE,
        SCAN_RECEIVING_TMS,
        SCAN_RECEIVING_TDI,
        SCAN_PROCESSING,
        SCAN_SENDING_TDO
    };
    ScanState scan_state;
    uint32_t scan_num_bits;
    uint32_t scan_num_bytes;
    uint32_t scan_bit_index;
    uint8_t scan_tms_buf[512];
    uint8_t scan_tdi_buf[512];
    uint8_t scan_tdo_buf[512];
    uint32_t scan_bytes_received;
    uint32_t scan_bytes_sent;

    // Legacy protocol handlers
    void process_command(struct vpi_cmd* cmd, struct vpi_resp* resp);
    void process_scan(uint32_t num_bits);
    void continue_scan();

    // OpenOCD protocol handlers
    void process_vpi_packet();
    void send_minimal_response(uint8_t response, uint8_t tdo_val, uint8_t mode, uint8_t status);
    void continue_vpi_work();
    void close_connection();
};

#endif // JTAG_VPI_SERVER_H
