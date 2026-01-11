/**
 * JTAG VPI Server Header
 */

#ifndef JTAG_VPI_SERVER_H
#define JTAG_VPI_SERVER_H

#include <stdint.h>

class JtagVpiServer {
public:
    JtagVpiServer(int port = 3333);
    ~JtagVpiServer();
    
    bool init();
    void poll();
    void update_signals(uint8_t tdo, uint32_t idcode, uint8_t mode);
    bool get_pending_signals(uint8_t* tms, uint8_t* tdi, uint8_t* mode_sel, bool* tck_pulse);
    bool is_client_connected() const { return client_sock >= 0; }
    
private:
    int port;
    int server_sock;
    int client_sock;
    
    // Current signal values
    uint8_t current_tdo;
    uint32_t current_idcode;
    uint8_t current_mode;
    
    // Pending commands from client
    uint8_t pending_tms;
    uint8_t pending_tdi;
    uint8_t pending_mode_select;
    bool pending_tck_pulse;
    
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
    
    void process_command(struct vpi_cmd* cmd, struct vpi_resp* resp);
    void process_scan(uint32_t num_bits);
    void continue_scan();
    void close_connection();
};

#endif // JTAG_VPI_SERVER_H
