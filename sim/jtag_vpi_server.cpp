/**
 * JTAG VPI Server for Verilator
 * Provides TCP/IP socket interface for external JTAG control
 */

#include "jtag_vpi_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

// Legacy 8-byte protocol structures (kept for backwards-compat tests)
// Command format (8 bytes)
struct vpi_cmd {
    uint8_t cmd;      // Command type: 0=RESET, 2=SCAN, 3=SET_PORT
    uint8_t pad[3];   // Reserved
    uint32_t length;  // For SCAN: number of bits to shift
} __attribute__((packed));

// Scan data follows the command in separate packets
// For CMD_SCAN (0x02): Client sends TMS buffer, then TDI buffer
// Server responds with TDO buffer

struct vpi_resp {
    uint8_t response;
    uint8_t tdo_val;
    uint8_t mode;
    uint8_t status;
};

JtagVpiServer::JtagVpiServer(int port) 
    : port(port), server_sock(-1), client_sock(-1),
      scan_state(SCAN_IDLE), scan_num_bits(0), scan_num_bytes(0),
            scan_bit_index(0), scan_bytes_received(0), scan_bytes_sent(0) {
    pending_tms = 0;
    pending_tdi = 0;
    pending_mode_select = 0;
    pending_tck_pulse = false;
        reset_pulses_remaining = 0;
    current_tdo = 0;
    current_idcode = 0;
    current_mode = 0;
    // Initialize command buffer
    memset(cmd_buf, 0, sizeof(cmd_buf));
    cmd_bytes_received = 0;
    // Init OpenOCD vpi packet state
    memset(&vpi_cmd_rx, 0, sizeof(vpi_cmd_rx));
    memset(&vpi_cmd_tx, 0, sizeof(vpi_cmd_tx));
}

JtagVpiServer::~JtagVpiServer() {
    close_connection();
    if (server_sock >= 0) {
        close(server_sock);
    }
}

bool JtagVpiServer::init() {
    struct sockaddr_in addr;
    
    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        printf("[VPI] Failed to create socket\n");
        return false;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Make socket non-blocking
    int flags = fcntl(server_sock, F_GETFL, 0);
    fcntl(server_sock, F_SETFL, flags | O_NONBLOCK);
    
    // Bind
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    
    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("[VPI] Failed to bind to port %d\n", port);
        close(server_sock);
        server_sock = -1;
        return false;
    }
    
    // Listen
    if (listen(server_sock, 1) < 0) {
        printf("[VPI] Failed to listen\n");
        close(server_sock);
        server_sock = -1;
        return false;
    }
    
    printf("[VPI] Server listening on 127.0.0.1:%d\n", port);
    return true;
}

void JtagVpiServer::poll() {
    // Try to accept new connection if not connected
    if (client_sock < 0) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock >= 0) {
            printf("[VPI] Client connected from %s:%d\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            fflush(stdout);
            // Keep socket non-blocking
            int flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);
        }
        return;
    }
    
    // Continue any ongoing operations
    if (protocol_mode == PROTO_OPENOCD_VPI) {
        continue_vpi_work();
    } else {
        if (scan_state != SCAN_IDLE) {
            continue_scan();
            return;
        }
    }
    
    // If OpenOCD VPI mode: receive full fixed-size packets
    if (protocol_mode == PROTO_OPENOCD_VPI) {
        if (vpi_rx_bytes < VPI_PKT_SIZE) {
            ssize_t ret = recv(client_sock, ((uint8_t*)&vpi_cmd_rx) + vpi_rx_bytes,
                               VPI_PKT_SIZE - vpi_rx_bytes, MSG_DONTWAIT);
            if (ret < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    printf("[VPI] Connection error: %s\n", strerror(errno));
                    close_connection();
                }
                return;
            }
            if (ret == 0) {
                printf("[VPI] Client disconnected\n");
                close_connection();
                return;
            }
            vpi_rx_bytes += ret;
            if (vpi_rx_bytes < VPI_PKT_SIZE) {
                return; // wait for rest of packet
            }
        }

        // Full packet received
        process_vpi_packet();
        vpi_rx_bytes = 0;
        memset(&vpi_cmd_rx, 0, sizeof(vpi_cmd_rx));
        return;
    }

    // Auto-detect protocol if unknown: read some bytes and infer
    if (protocol_mode == PROTO_UNKNOWN) {
        if (vpi_rx_bytes < VPI_PKT_SIZE) {
            ssize_t ret = recv(client_sock, ((uint8_t*)&vpi_cmd_rx) + vpi_rx_bytes,
                               VPI_PKT_SIZE - vpi_rx_bytes, MSG_DONTWAIT);
            if (ret < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    printf("[VPI] Connection error: %s\n", strerror(errno));
                    close_connection();
                }
                return;
            }
            if (ret == 0) {
                printf("[VPI] Client disconnected\n");
                close_connection();
                return;
            }
            vpi_rx_bytes += ret;
        }

        // Heuristic: legacy sends 8-byte header then waits; OpenOCD sends full 1036 bytes
        if (vpi_rx_bytes > 8) {
            protocol_mode = PROTO_OPENOCD_VPI;
            // Keep accumulated bytes and proceed in next branch
        } else if (vpi_rx_bytes == 8) {
            // Legacy mode detected
            protocol_mode = PROTO_LEGACY_8BYTE;
            // Hand off the first 8 bytes to legacy parser
            memcpy(cmd_buf, &vpi_cmd_rx, 8);
            cmd_bytes_received = 8;
            vpi_rx_bytes = 0;
            memset(&vpi_cmd_rx, 0, sizeof(vpi_cmd_rx));
        } else {
            // Need more data to decide
            return;
        }
    }

    // Legacy protocol: Process new commands, handling partial reads of the 8-byte header
    if (cmd_bytes_received < sizeof(vpi_cmd)) {
        ssize_t ret = recv(client_sock, cmd_buf + cmd_bytes_received,
                           sizeof(vpi_cmd) - cmd_bytes_received, MSG_DONTWAIT);
        if (ret < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                printf("[VPI] Connection error: %s\n", strerror(errno));
                close_connection();
            }
            return;
        }
        if (ret == 0) {
            printf("[VPI] Client disconnected\n");
            close_connection();
            return;
        }
        cmd_bytes_received += ret;
        if (cmd_bytes_received < sizeof(vpi_cmd)) {
            // Wait for remaining bytes of the command
            return;
        }
    }

    // We have a full command header buffered
    vpi_cmd cmd;
    memcpy(&cmd, cmd_buf, sizeof(cmd));
    // Reset for next command header
    cmd_bytes_received = 0;

    // Process command
    vpi_resp resp;
    process_command(&cmd, &resp);
}
static inline uint32_t le32_to_host(const uint8_t b[4]) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static inline void host_to_le32(uint8_t b[4], uint32_t v) {
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
}

// Handle a full OpenOCD VPI packet
void JtagVpiServer::process_vpi_packet() {
    uint32_t cmd = le32_to_host(vpi_cmd_rx.cmd_buf);
    uint32_t length = le32_to_host(vpi_cmd_rx.length_buf);
    uint32_t nb_bits = le32_to_host(vpi_cmd_rx.nb_bits_buf);

    switch (cmd) {
        case 0: { // CMD_RESET
            reset_pulses_remaining = 6;
            pending_tms = 1;
            pending_tdi = 0;
            pending_tck_pulse = true;
            break;
        }
        case 1: { // CMD_TMS_SEQ
            // Copy TMS bits and start sequence
            tms_seq_active = true;
            tms_seq_num_bits = nb_bits;
            tms_seq_bit_index = 0;
            uint32_t nb_bytes = (nb_bits + 7) / 8;
            if (nb_bytes > sizeof(tms_seq_buf)) nb_bytes = sizeof(tms_seq_buf);
            memcpy(tms_seq_buf, vpi_cmd_rx.buffer_out, nb_bytes);
            break;
        }
        case 2: // CMD_SCAN_CHAIN
        case 3: { // CMD_SCAN_CHAIN_FLIP_TMS
            // Initialize scan using RX data; reuse legacy scan state machine
            scan_num_bits = nb_bits;
            scan_num_bytes = (nb_bits + 7) / 8;
            scan_bit_index = 0;
            scan_bytes_received = scan_num_bytes; // mark buffers as ready
            scan_bytes_sent = 0;
            memset(scan_tdo_buf, 0, sizeof(scan_tdo_buf));
            // For OpenOCD, TMS is 0 for all bits, except last bit when cmd==3
            memset(scan_tms_buf, 0x00, scan_num_bytes);
            if (cmd == 3 && nb_bits > 0) {
                uint32_t last = nb_bits - 1;
                scan_tms_buf[last / 8] |= (1u << (last % 8));
            }
            memcpy(scan_tdi_buf, vpi_cmd_rx.buffer_out, scan_num_bytes);
            // Enter processing state (legacy engine)
            scan_state = SCAN_PROCESSING;
            // Prepare TX packet header for when we send the response
            memset(&vpi_cmd_tx, 0, sizeof(vpi_cmd_tx));
            host_to_le32(vpi_cmd_tx.cmd_buf, cmd);
            host_to_le32(vpi_cmd_tx.length_buf, scan_num_bytes);
            host_to_le32(vpi_cmd_tx.nb_bits_buf, nb_bits);
            vpi_tx_bytes = 0;
            vpi_tx_pending = false;
            break;
        }
        case 4: { // CMD_STOP_SIMU
            // Optionally close connection
            close_connection();
            break;
        }
        case 5: { // CMD_OSCAN1 - two-wire cJTAG/OScan1 operation
            // Format: buffer_out[0] = (tckc & 1) | ((tmsc & 1) << 1)
            // Extract TCKC and TMSC bits
            uint8_t tckc = vpi_cmd_rx.buffer_out[0] & 1;
            uint8_t tmsc = (vpi_cmd_rx.buffer_out[0] >> 1) & 1;
            
            // In two-wire mode:
            // - TCKC maps to TCK
            // - TMSC is bidirectional: input for TMS/TDI, output for TDO
            // For now, treat TMSC as TMS (simplification)
            pending_tms = tmsc;
            pending_tdi = tmsc;  // In SF0, TDI comes on falling edge
            pending_tck_pulse = true;
            
            // Prepare response with TDO on TMSC
            memset(&vpi_cmd_tx, 0, sizeof(vpi_cmd_tx));
            host_to_le32(vpi_cmd_tx.cmd_buf, 5);
            host_to_le32(vpi_cmd_tx.length_buf, 1);
            host_to_le32(vpi_cmd_tx.nb_bits_buf, 2);
            vpi_cmd_tx.buffer_in[0] = current_tdo & 1;  // TDO bit on TMSC
            
            // Queue response
            vpi_tx_pending = true;
            vpi_tx_bytes = 0;
            break;
        }
        default:
            // Unknown - ignore
            break;
    }
}

// Advance OpenOCD work items (TMS_SEQ/SCAN processing and TX)
void JtagVpiServer::continue_vpi_work() {
    // 1) If sending a response, try to flush it first
    if (vpi_tx_pending && client_sock >= 0) {
        ssize_t sent = send(client_sock, ((uint8_t*)&vpi_cmd_tx) + vpi_tx_bytes,
                            VPI_PKT_SIZE - vpi_tx_bytes, MSG_DONTWAIT);
        if (sent > 0) {
            vpi_tx_bytes += sent;
            if (vpi_tx_bytes >= VPI_PKT_SIZE) {
                vpi_tx_pending = false;
                vpi_tx_bytes = 0;
            }
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            close_connection();
            return;
        }
    }

    // 2) Process TMS sequence (no response expected)
    if (tms_seq_active) {
        if (pending_tck_pulse) return; // wait for pulse to complete
        if (tms_seq_bit_index < tms_seq_num_bits) {
            uint32_t i = tms_seq_bit_index;
            uint8_t bit = (tms_seq_buf[i / 8] >> (i % 8)) & 1;
            pending_tms = bit;
            pending_tdi = 0;
            pending_tck_pulse = true;
            tms_seq_bit_index++;
        } else {
            tms_seq_active = false;
        }
        return;
    }

    // 3) If legacy scan state machine is active, let it progress
    if (scan_state == SCAN_PROCESSING || scan_state == SCAN_SENDING_TDO) {
        // Run legacy per-bit engine
        if (scan_state == SCAN_PROCESSING && pending_tck_pulse) return;
        continue_scan();
        // When legacy finishes sending TDO bytes, prepare and queue full response
        if (scan_state == SCAN_IDLE && !vpi_tx_pending && client_sock >= 0) {
            // Fill TX buffer_in with captured TDO
            memcpy(vpi_cmd_tx.buffer_in, scan_tdo_buf, scan_num_bytes);
            // Transmit full packet (OpenOCD expects fixed-size)
            vpi_tx_pending = true;
            vpi_tx_bytes = 0;
        }
        return;
    }
}

void JtagVpiServer::process_command(vpi_cmd* cmd, vpi_resp* resp) {
    memset(resp, 0, sizeof(*resp));
    
    // Convert length from network byte order (big-endian) to host byte order
    uint32_t length = ntohl(cmd->length);
    
    static int debug_cmds = 0;
    if (debug_cmds < 10) {
        printf("[VPI][DBG] CMD=0x%02x len=%u\n", cmd->cmd, length);
        debug_cmds++;
    }

    // Validate command - if we see garbage commands with huge lengths, we're out of sync
    if (cmd->cmd > 0x0F || (length > 4096 && cmd->cmd != 0x02)) {
        resp->response = 1;  // Error
        return;
    }

    bool send_resp = true;
    switch (cmd->cmd) {
        case 0x00:  // CMD_RESET - JTAG reset
            // Reset JTAG state machine - set TMS high for 5+ clocks
            reset_pulses_remaining = 6;
            pending_tms = 1;
            pending_tdi = 0;
            pending_tck_pulse = true;  // kick off the first pulse immediately
            // Send simple ACK response
            resp->response = 0;  // OK
            resp->tdo_val = current_tdo;
            break;
            
        case 0x02:  // CMD_SCAN - Scan operation
            // OpenOCD will send TMS buffer, then TDI buffer
            // We need to receive them and shift through JTAG
            static int debug_scan_cmds = 0;
            if (debug_scan_cmds < 5) {
                printf("[VPI][DBG] CMD_SCAN bits=%u (bytes=%u)\n", length, (length + 7) / 8);
                debug_scan_cmds++;
            }
            process_scan(length);
            // Acknowledge the SCAN command so client can proceed
            resp->response = 0;  // OK
            resp->tdo_val = current_tdo;
            break;
            
        case 0x03:  // CMD_SET_PORT - Configuration
            resp->response = 0;  // OK
            break;
        
        case 0x05:  // CMD_OSCAN1 - two-wire operation (legacy protocol path)
            // In legacy 8-byte protocol, we don't have payload yet
            // For now, just ACK and let higher level handle it
            resp->response = 0;  // OK
            resp->tdo_val = current_tdo;
            break;
            
        default:
            resp->response = 1;  // Error
            break;
    }
    
    // Send response back to client
    if (send_resp && client_sock >= 0) {
        ssize_t ret = send(client_sock, resp, sizeof(*resp), MSG_DONTWAIT);
        if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            close_connection();
        }
    }
}

void JtagVpiServer::process_scan(uint32_t num_bits) {
    if (num_bits == 0 || num_bits > 4096) {
        return;
    }
    
    scan_num_bits = num_bits;
    scan_num_bytes = (num_bits + 7) / 8;
    scan_bit_index = 0;
    scan_bytes_received = 0;
    scan_bytes_sent = 0;
    memset(scan_tms_buf, 0, sizeof(scan_tms_buf));
    memset(scan_tdi_buf, 0, sizeof(scan_tdi_buf));
    memset(scan_tdo_buf, 0, sizeof(scan_tdo_buf));
    
    scan_state = SCAN_RECEIVING_TMS;
}

void JtagVpiServer::continue_scan() {
    ssize_t ret;
    
    switch (scan_state) {
        case SCAN_RECEIVING_TMS:
            // Try to receive TMS buffer
            ret = recv(client_sock, scan_tms_buf + scan_bytes_received, 
                      scan_num_bytes - scan_bytes_received, MSG_DONTWAIT);
            if (ret > 0) {
                scan_bytes_received += ret;
                if (scan_bytes_received >= scan_num_bytes) {
                    scan_bytes_received = 0;
                    scan_state = SCAN_RECEIVING_TDI;
                }
            } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                scan_state = SCAN_IDLE;
                close_connection();
            }
            break;
            
        case SCAN_RECEIVING_TDI:
            // Try to receive TDI buffer
            ret = recv(client_sock, scan_tdi_buf + scan_bytes_received,
                      scan_num_bytes - scan_bytes_received, MSG_DONTWAIT);
            if (ret > 0) {
                scan_bytes_received += ret;
                if (scan_bytes_received >= scan_num_bytes) {
                    scan_bit_index = 0;
                    scan_state = SCAN_PROCESSING;
                }
            } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                scan_state = SCAN_IDLE;
                close_connection();
            }
            break;
            
        case SCAN_PROCESSING:
            // State machine for processing bits:
            // 1. If pending_tck_pulse is true: TCK pulse is in progress, wait for it to complete
            // 2. If pending_tck_pulse is false and scan_bit_index > 0: capture TDO from last bit
            // 3. Request TCK pulse for next bit
            
            // If a TCK pulse is still pending, wait for simulation to complete it
            if (pending_tck_pulse) {
                return;
            }
            
            // TCK pulse from previous bit has completed - capture the TDO value
            if (scan_bit_index > 0) {
                uint32_t prev_bit = scan_bit_index - 1;
                uint32_t prev_byte_idx = prev_bit / 8;
                uint32_t prev_bit_idx = prev_bit % 8;
                uint32_t prev_bit_pos = msb_first ? (7 - prev_bit_idx) : prev_bit_idx;
                if (current_tdo) {
                    scan_tdo_buf[prev_byte_idx] |= (1 << prev_bit_pos);
                } else {
                    scan_tdo_buf[prev_byte_idx] &= ~(1 << prev_bit_pos);
                }
            }
            
            while (scan_bit_index < scan_num_bits && pending_tck_pulse == false) {
                uint32_t byte_idx = scan_bit_index / 8;
                uint32_t bit_idx = scan_bit_index % 8;
                uint32_t bit_pos = msb_first ? (7 - bit_idx) : bit_idx;
                
                // Extract TMS and TDI from buffers
                uint8_t tms_bit = (scan_tms_buf[byte_idx] >> bit_pos) & 1;
                uint8_t tdi_bit = (scan_tdi_buf[byte_idx] >> bit_pos) & 1;
                
                // Request TCK pulse for this bit
                pending_tms = tms_bit;
                pending_tdi = tdi_bit;
                pending_tck_pulse = true;
                scan_bit_index++;
                return;  // Return to let simulation execute the TCK pulse
            }
            
            // All bits processed
            if (scan_bit_index >= scan_num_bits) {
                // Capture the last TDO bit
                if (scan_bit_index > 0) {
                    uint32_t last_bit = scan_bit_index - 1;
                    uint32_t last_byte_idx = last_bit / 8;
                    uint32_t last_bit_idx = last_bit % 8;
                    uint32_t last_bit_pos = msb_first ? (7 - last_bit_idx) : last_bit_idx;
                    if (current_tdo) {
                        scan_tdo_buf[last_byte_idx] |= (1 << last_bit_pos);
                    } else {
                        scan_tdo_buf[last_byte_idx] &= ~(1 << last_bit_pos);
                    }
                }
                scan_bytes_sent = 0;
                scan_state = SCAN_SENDING_TDO;
            }
            break;
            
        case SCAN_SENDING_TDO:
            // Send TDO buffer as response packets
            // Send up to all bytes in one go since non-blocking might handle it
            ret = send(client_sock, scan_tdo_buf + scan_bytes_sent,
                      scan_num_bytes - scan_bytes_sent, MSG_DONTWAIT);
            if (ret > 0) {
                scan_bytes_sent += ret;
                if (scan_bytes_sent >= scan_num_bytes) {
                    static int debug_scans = 0;
                    if (debug_scans < 3) {
                        printf("[VPI][DBG] SCAN bits=%u bytes=%u TDO[0]=0x%02x TDO[1]=0x%02x\n",
                               scan_num_bits,
                               scan_num_bytes,
                               scan_tdo_buf[0],
                               scan_tdo_buf[1]);
                        debug_scans++;
                    }
                    scan_state = SCAN_IDLE;
                }
            } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                scan_state = SCAN_IDLE;
                close_connection();
            }
            break;
            
        default:
            scan_state = SCAN_IDLE;
            break;
    }
}

void JtagVpiServer::close_connection() {
    if (client_sock >= 0) {
        close(client_sock);
        client_sock = -1;
    }
}

void JtagVpiServer::update_signals(uint8_t tdo, uint32_t idcode, uint8_t mode) {
    current_tdo = tdo;
    current_idcode = idcode;
    current_mode = mode;
}

void JtagVpiServer::update_signals(uint8_t tdo, uint8_t tdo_en, uint32_t idcode, uint8_t mode) {
    current_tdo = tdo;
    current_tdo_en = tdo_en;
    current_idcode = idcode;
    current_mode = mode;
}

bool JtagVpiServer::get_pending_signals(uint8_t* tms, uint8_t* tdi, uint8_t* mode_sel, bool* tck_pulse) {
    // Handle queued reset pulses first
    if (reset_pulses_remaining > 0) {
        *tms = 1;
        *tdi = 0;
        *mode_sel = pending_mode_select;
        *tck_pulse = true;
        reset_pulses_remaining--;
        return true;
    }

    if (!pending_tck_pulse && pending_mode_select == current_mode) {
        return false;
    }
    
    *tms = pending_tms;
    *tdi = pending_tdi;
    *mode_sel = pending_mode_select;
    *tck_pulse = pending_tck_pulse;
    
    pending_tck_pulse = false;
    
    return true;
}
