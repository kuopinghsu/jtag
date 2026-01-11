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

// OpenOCD jtag_vpi protocol structures
// Command format from OpenOCD (8 bytes)
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
    current_tdo = 0;
    current_idcode = 0;
    current_mode = 0;
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
    
    // Continue ongoing scan operation
    if (scan_state != SCAN_IDLE) {
        continue_scan();
        return;
    }
    
    // Process new commands
    vpi_cmd cmd;
    ssize_t ret = recv(client_sock, &cmd, sizeof(cmd), MSG_DONTWAIT);
    
    if (ret < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Connection error
            printf("[VPI] Connection error: %s\n", strerror(errno));
            close_connection();
        }
        return;
    }
    
    if (ret == 0) {
        // Client disconnected
        printf("[VPI] Client disconnected\n");
        close_connection();
        return;
    }
    
    if (ret != sizeof(cmd)) {
        // Partial read - this should not happen with TCP
        printf("[VPI] WARNING: Partial read - got %zd bytes, expected %zu bytes\n",
               ret, sizeof(cmd));
        return;
    }
    
    // Process command
    vpi_resp resp;
    process_command(&cmd, &resp);
}

void JtagVpiServer::process_command(vpi_cmd* cmd, vpi_resp* resp) {
    memset(resp, 0, sizeof(*resp));
    
    // Convert length from network byte order (big-endian) to host byte order
    uint32_t length = ntohl(cmd->length);
    
    // Validate command - if we see garbage commands with huge lengths, we're out of sync
    if (cmd->cmd > 0x0F || (length > 4096 && cmd->cmd != 0x02)) {
        printf("[VPI] WARNING: Possible protocol desync - cmd=0x%02x, length=%u (0x%08x)\n", 
               cmd->cmd, length, cmd->length);
        resp->response = 1;  // Error
        fflush(stdout);
        return;
    }
    
    printf("[VPI] Command: 0x%02x, length: %u bits\n", cmd->cmd, length);
    fflush(stdout);
    
    switch (cmd->cmd) {
        case 0x00:  // CMD_RESET - JTAG reset
            printf("[VPI] CMD_RESET\n");
            // Reset JTAG state machine - set TMS high for 5+ clocks
            for (int i = 0; i < 6; i++) {
                pending_tms = 1;
                pending_tdi = 0;
                pending_tck_pulse = true;
            }
            // Send simple ACK response
            resp->response = 0;  // OK
            resp->tdo_val = current_tdo;
            break;
            
        case 0x02:  // CMD_SCAN - Scan operation
            printf("[VPI] CMD_SCAN: %u bits\n", length);
            // OpenOCD will send TMS buffer, then TDI buffer
            // We need to receive them and shift through JTAG
            process_scan(length);
            // For now, send a simple response
            resp->response = 0;  // OK
            resp->tdo_val = current_tdo;
            break;
            
        case 0x03:  // CMD_SET_PORT - Configuration
            printf("[VPI] CMD_SET_PORT\n");
            resp->response = 0;  // OK
            break;
            
        default:
            printf("[VPI] Unknown command: 0x%02x (length=%u)\n", cmd->cmd, length);
            fflush(stdout);
            resp->response = 1;  // Error
            break;
    }
    
    // Send response back to client
    if (client_sock >= 0) {
        ssize_t ret = send(client_sock, resp, sizeof(*resp), MSG_DONTWAIT);
        if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("[VPI] Send error: %s\n", strerror(errno));
            close_connection();
        } else if (ret > 0) {
            printf("[VPI] Sent response: 0x%02x (tdo=0x%02x)\n", resp->response, resp->tdo_val);
            fflush(stdout);
        }
    }
}

void JtagVpiServer::process_scan(uint32_t num_bits) {
    if (num_bits == 0 || num_bits > 4096) {
        printf("[VPI] Invalid scan length: %u\n", num_bits);
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
    printf("[VPI] Starting scan: %u bits (%u bytes)\n", scan_num_bits, scan_num_bytes);
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
                    printf("[VPI] Received TMS buffer: %u bytes\n", scan_bytes_received);
                    scan_bytes_received = 0;
                    scan_state = SCAN_RECEIVING_TDI;
                }
            } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                printf("[VPI] Error receiving TMS: %s\n", strerror(errno));
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
                    printf("[VPI] Received TDI buffer: %u bytes\n", scan_bytes_received);
                    scan_bit_index = 0;
                    scan_state = SCAN_PROCESSING;
                }
            } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                printf("[VPI] Error receiving TDI: %s\n", strerror(errno));
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
                if (current_tdo) {
                    scan_tdo_buf[prev_byte_idx] |= (1 << prev_bit_idx);
                } else {
                    scan_tdo_buf[prev_byte_idx] &= ~(1 << prev_bit_idx);
                }
            }
            
            while (scan_bit_index < scan_num_bits && pending_tck_pulse == false) {
                uint32_t byte_idx = scan_bit_index / 8;
                uint32_t bit_idx = scan_bit_index % 8;
                
                // Extract TMS and TDI from buffers
                uint8_t tms_bit = (scan_tms_buf[byte_idx] >> bit_idx) & 1;
                uint8_t tdi_bit = (scan_tdi_buf[byte_idx] >> bit_idx) & 1;
                
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
                    if (current_tdo) {
                        scan_tdo_buf[last_byte_idx] |= (1 << last_bit_idx);
                    } else {
                        scan_tdo_buf[last_byte_idx] &= ~(1 << last_bit_idx);
                    }
                }
                printf("[VPI] Scan processing complete: %u bits processed\n", scan_num_bits);
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
                printf("[VPI] Sent %zd TDO bytes (%u/%u total)\n", ret, scan_bytes_sent, scan_num_bytes);
                if (scan_bytes_sent >= scan_num_bytes) {
                    printf("[VPI] ✓ Scan complete: %u bits processed, TDO sent\n", scan_num_bits);
                    // Show what we captured
                    if (scan_num_bytes <= 4) {
                        uint32_t tdo_val = 0;
                        for (uint32_t i = 0; i < scan_num_bytes && i < 4; i++) {
                            tdo_val |= (scan_tdo_buf[i] << (i * 8));
                        }
                        printf("[VPI] TDO value: 0x%08x\n", tdo_val);
                    }
                    scan_state = SCAN_IDLE;
                }
            } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                printf("[VPI] Error sending TDO: %s\n", strerror(errno));
                scan_state = SCAN_IDLE;
                close_connection();
            }
            fflush(stdout);
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

bool JtagVpiServer::get_pending_signals(uint8_t* tms, uint8_t* tdi, uint8_t* mode_sel, bool* tck_pulse) {
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
