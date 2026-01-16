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

// Debug macros - controlled by debug_level
#define DBG_PRINT(level, ...) \
    do { if (debug_level >= (level)) { printf(__VA_ARGS__); fflush(stdout); } } while(0)

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
    : port(port),
      server_sock(-1),
      client_sock(-1),
      current_tdo(0),
      current_tdo_en(0),
      current_idcode(0),
      current_mode(0),
      msb_first(false),
      debug_level(0),
      scan_state(SCAN_IDLE),
      scan_is_legacy(true),
      scan_num_bits(0),
      scan_num_bytes(0),
      scan_bit_index(0),
      scan_bytes_received(0),
      scan_bytes_sent(0) {
    pending_tms = 0;
    pending_tdi = 0;
    pending_mode_select = 0;  // Will be set by set_mode() from command-line
    pending_tck_pulse = false;
    reset_pulses_remaining = 0;
    tckc_state = 0;
    pending_tckc_toggle = false;
    tckc_toggle_consumed = false;  // Initialize SF0 synchronization flag
    // Initialize command buffer
    memset(cmd_buf, 0, sizeof(cmd_buf));
    cmd_bytes_received = 0;
    // Init OpenOCD vpi packet state
    memset(&vpi_cmd_rx, 0, sizeof(vpi_cmd_rx));
    memset(&vpi_cmd_tx, 0, sizeof(vpi_cmd_tx));
    memset(&minimal_cmd_rx, 0, sizeof(minimal_cmd_rx));
    minimal_rx_bytes = 0;
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
    // For OpenOCD VPI mode, always use continue_vpi_work() which handles scans
    if (protocol_mode == PROTO_OPENOCD_VPI) {
        continue_vpi_work();
        return;
    }

    // For legacy protocol: check scan state and call continue_scan()
    if (scan_state != SCAN_IDLE) {
        continue_scan();
        return;
    }

    // Auto-detect protocol if unknown: read some bytes and infer (use small buffer)
    if (protocol_mode == PROTO_UNKNOWN) {
        DBG_PRINT(2, "[VPI][DBG] Protocol detection: minimal_rx_bytes=%d\n", minimal_rx_bytes);
        if (minimal_rx_bytes < sizeof(minimal_cmd_rx)) {
            ssize_t ret = recv(client_sock, ((uint8_t*)&minimal_cmd_rx) + minimal_rx_bytes,
                               sizeof(minimal_cmd_rx) - minimal_rx_bytes, MSG_DONTWAIT);
            if (ret < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    printf("[VPI] Connection error during protocol detection: %s\n", strerror(errno));
                    fflush(stdout);
                    close_connection();
                }
                return;
            }
            if (ret == 0) {
                printf("[VPI] Client disconnected during protocol detection\n");
                fflush(stdout);
                close_connection();
                return;
            }
            minimal_rx_bytes += ret;
            DBG_PRINT(2, "[VPI][DBG] Received %zd bytes, total=%d\n", ret, minimal_rx_bytes);
        }

        if (minimal_rx_bytes >= 1) {
            uint8_t cmd_byte = minimal_cmd_rx.cmd;
            DBG_PRINT(2, "[VPI][DBG] Protocol detection: cmd_byte=0x%02x, bytes=%d\n", cmd_byte, minimal_rx_bytes);

            if (minimal_rx_bytes >= sizeof(minimal_cmd_rx)) {
                // Have at least 8 bytes - decide between minimal 8-byte flow vs full 1036-byte OpenOCD packet
                uint8_t peek_buf[16];
                ssize_t peek_ret = recv(client_sock, peek_buf, sizeof(peek_buf), MSG_DONTWAIT | MSG_PEEK);
                bool more_data_available = (peek_ret > 0);

                protocol_mode = PROTO_OPENOCD_VPI;

                if (more_data_available) {
                    // More data already buffered on the socket - treat as full OpenOCD packet
                    DBG_PRINT(1, "[VPI][DBG] OpenOCD protocol detected (cmd=0x%02x), waiting for full packet\n", cmd_byte);
                    memcpy(&vpi_cmd_rx, &minimal_cmd_rx, sizeof(minimal_cmd_rx));
                    vpi_rx_bytes = sizeof(minimal_cmd_rx);
                    minimal_rx_bytes = 0;
                    memset(&minimal_cmd_rx, 0, sizeof(minimal_cmd_rx));
                    vpi_minimal_mode = false;  // Expect 1036-byte packet
                } else {
                    // No extra data yet - stay in minimal (8-byte) mode for legacy/simple clients
                    DBG_PRINT(1, "[VPI][DBG] Minimal 8-byte protocol detected (cmd=0x%02x)\n", cmd_byte);
                    vpi_minimal_mode = true;
                }
            } else {
                return;
            }
        } else {
            return;
        }
    }

    // After detection, continue with the detected protocol
    // Handle OpenOCD VPI mode (continue if just detected)
    if (protocol_mode == PROTO_OPENOCD_VPI) {
        // OpenOCD mode can accept both:
        // 1. Minimal 8-byte commands (cmd + pad + length)
        // 2. Full 1036-byte packets with data buffers

        // Minimal path: process immediately when flagged
        if (vpi_minimal_mode) {
            if (minimal_rx_bytes < sizeof(minimal_cmd_rx)) {
                ssize_t ret = recv(client_sock, ((uint8_t*)&minimal_cmd_rx) + minimal_rx_bytes,
                                   sizeof(minimal_cmd_rx) - minimal_rx_bytes, MSG_DONTWAIT);
                if (ret < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        printf("[VPI] Connection error (minimal): %s\n", strerror(errno));
                        close_connection();
                    }
                    return;
                }
                if (ret == 0) {
                    printf("[VPI] Client disconnected (minimal)\n");
                    close_connection();
                    return;
                }
                minimal_rx_bytes += ret;
            }

            if (minimal_rx_bytes < sizeof(minimal_cmd_rx)) {
                return; // wait for complete minimal packet
            }

            process_vpi_packet();
            DBG_PRINT(2, "[VPI][DBG] Minimal packet processed, resetting rx buffer\n");
            minimal_rx_bytes = 0;
            memset(&minimal_cmd_rx, 0, sizeof(minimal_cmd_rx));
            return;
        }

        // Read until we have at least 8 bytes (minimum OpenOCD command)
        if (vpi_rx_bytes < 8) {
            ssize_t ret = recv(client_sock, ((uint8_t*)&vpi_cmd_rx) + vpi_rx_bytes,
                               8 - vpi_rx_bytes, MSG_DONTWAIT);
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
            if (vpi_rx_bytes < 8) {
                return; // wait for rest of minimal command
            }
        }

        // We have at least 8 bytes - continue reading until we have full packet
        // OpenOCD VPI packets are 1036 bytes, not 8 bytes
        // DO NOT treat 8 bytes as complete - must read full packet
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
        DBG_PRINT(2, "[VPI][DBG] Full VPI packet received, processing...\n");
        process_vpi_packet();
        DBG_PRINT(2, "[VPI][DBG] Packet processed, resetting buffer\n");
        vpi_rx_bytes = 0;
        memset(&vpi_cmd_rx, 0, sizeof(vpi_cmd_rx));
        return;
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

static inline uint32_t be32_to_host(const uint8_t b[4]) {
    // Minimal 8-byte protocol uses network order (big-endian) for length
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

// Send a minimal 4-byte response (for test_protocol compatibility)
void JtagVpiServer::send_minimal_response(uint8_t response, uint8_t tdo_val, uint8_t mode, uint8_t status) {
    MinimalVpiResp resp;
    resp.response = response;
    resp.tdo_val = tdo_val;
    resp.mode = mode;
    resp.status = status;

    // Send with non-blocking approach - avoid blocking/busy-wait that can cause timeouts
    size_t sent_total = 0;
    int retry_count = 0;
    const int MAX_RETRIES = 1000;  // Prevent infinite busy-wait

    DBG_PRINT(2, "[VPI][DBG] Sending minimal response: resp=0x%02x, tdo=0x%02x, mode=0x%02x, status=0x%02x\n",
              response, tdo_val, mode, status);

    while (sent_total < sizeof(resp) && retry_count < MAX_RETRIES) {
        ssize_t sent = send(client_sock, ((uint8_t*)&resp) + sent_total,
                           sizeof(resp) - sent_total, MSG_DONTWAIT);
        if (sent > 0) {
            sent_total += sent;
            DBG_PRINT(2, "[VPI][DBG] Sent %zd bytes, total=%zu/%zu\n", sent, sent_total, sizeof(resp));
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                retry_count++;
                if (retry_count >= MAX_RETRIES) {
                    DBG_PRINT(1, "[VPI][WARN] Send timeout after %d retries, but keeping connection alive\n", MAX_RETRIES);
                    return;  // Don't close connection on timeout
                }
                // Small delay to prevent busy-wait CPU consumption
                usleep(100); // 100 microseconds
            } else {
                // Classify different error types
                const char* error_type = "UNKNOWN";
                bool should_close = true;

                switch (errno) {
                    case EPIPE:
                        error_type = "BROKEN_PIPE";
                        break;
                    case ECONNRESET:
                        error_type = "CONNECTION_RESET";
                        break;
                    case ENOTCONN:
                        error_type = "NOT_CONNECTED";
                        break;
                    case EINTR:
                        error_type = "INTERRUPTED";
                        should_close = false;  // Recoverable
                        break;
                    default:
                        error_type = "OTHER";
                        should_close = (errno != EINTR);  // Be more tolerant
                        break;
                }

                DBG_PRINT(1, "[VPI][WARN] Send error (%s): errno=%d, %s%s\n",
                          error_type, errno, strerror(errno), should_close ? ", closing connection" : ", retrying");

                if (should_close) {
                    close_connection();
                    return;
                } else {
                    retry_count++;
                    usleep(1000); // 1ms delay for recoverable errors
                }
            }
        }
    }

    if (sent_total < sizeof(resp)) {
        DBG_PRINT(1, "[VPI][WARN] Incomplete send: %zu/%zu bytes sent, but keeping connection alive\n", sent_total, sizeof(resp));
        // Don't close connection for incomplete sends - may be recoverable
    } else {
        DBG_PRINT(2, "[VPI][DBG] Minimal response sent successfully\n");
        vpi_minimal_mode = true;
    }
}

// Handle a full OpenOCD VPI packet
void JtagVpiServer::process_vpi_packet() {
    uint32_t cmd, length, nb_bits;

    // In minimal mode, parse the 8-byte MinimalVpiCmd structure
    if (vpi_minimal_mode) {
        // MinimalVpiCmd: cmd(1) + pad(3) + length(4) = 8 bytes
        MinimalVpiCmd min_cmd;
        if (minimal_rx_bytes >= sizeof(MinimalVpiCmd)) {
            memcpy(&min_cmd, &minimal_cmd_rx, sizeof(MinimalVpiCmd));
        } else {
            memcpy(&min_cmd, &vpi_cmd_rx, sizeof(MinimalVpiCmd));
        }
        cmd = min_cmd.cmd;

        // Minimal protocol should be network-order, but some clients may send host-order.
        uint32_t len_be = be32_to_host(reinterpret_cast<uint8_t*>(&min_cmd.length));
        uint32_t len_le = le32_to_host(reinterpret_cast<uint8_t*>(&min_cmd.length));
        length = (len_be <= 4096) ? len_be : len_le;
        nb_bits = length;  // In minimal mode, length==nb_bits
        DBG_PRINT(2, "[VPI][DBG] Minimal mode parse: cmd=%u, length_be=%u, length_le=%u, chosen=%u, nb_bits=%u\n",
                  cmd, len_be, len_le, length, nb_bits);
    } else {
        // Full OpenOCD mode: parse the full 1036-byte OcdVpiCmd structure
        cmd = le32_to_host(vpi_cmd_rx.cmd_buf);
        length = le32_to_host(vpi_cmd_rx.length_buf);
        nb_bits = le32_to_host(vpi_cmd_rx.nb_bits_buf);
    }

    DBG_PRINT(1, "[VPI][DBG] process_vpi_packet: cmd=%u, length=%u, nb_bits=%u\n", cmd, length, nb_bits);

    switch (cmd) {
        case 0: { // CMD_RESET
            reset_pulses_remaining = 6;
            pending_tms = 1;
            pending_tdi = 0;
            pending_tck_pulse = true;

            // Send immediate response for minimal mode
            if (vpi_minimal_mode) {
                send_minimal_response(0x00, 0, current_mode, 0);
                // Clear any pending operations so next command can proceed cleanly
                reset_pulses_remaining = 0;
                pending_tck_pulse = false;
            } else {
                // Full OpenOCD mode - send empty response packet
                memset(&vpi_cmd_tx, 0, sizeof(vpi_cmd_tx));
                host_to_le32(vpi_cmd_tx.cmd_buf, cmd);
                host_to_le32(vpi_cmd_tx.length_buf, 0);
                host_to_le32(vpi_cmd_tx.nb_bits_buf, 0);
                vpi_tx_pending = true;
                vpi_tx_bytes = 0;
            }
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

            // Send response packet
            if (!vpi_minimal_mode) {
                memset(&vpi_cmd_tx, 0, sizeof(vpi_cmd_tx));
                host_to_le32(vpi_cmd_tx.cmd_buf, cmd);
                host_to_le32(vpi_cmd_tx.length_buf, 0);
                host_to_le32(vpi_cmd_tx.nb_bits_buf, 0);
                vpi_tx_pending = true;
                vpi_tx_bytes = 0;
            }
            break;
        }
        case 2: // CMD_SCAN_CHAIN
        case 3: { // CMD_SCAN_CHAIN_FLIP_TMS (or CMD_SET_PORT in minimal mode)
            // In minimal mode, cmd=0x03 is CMD_SET_PORT (mode query), not SCAN_CHAIN_FLIP_TMS
            if (vpi_minimal_mode && cmd == 3) {
                // Mode query - just return current mode
                send_minimal_response(0x00, current_tdo, current_mode, 0);
                break;
            }

            // In minimal mode, use legacy-style protocol (send response, then TMS/TDI/TDO exchange)
            if (vpi_minimal_mode) {
                // Send immediate response
                send_minimal_response(0x00, current_tdo, current_mode, 0);
                // Enter legacy scan state machine
                if (nb_bits == 0 || nb_bits > 4096) {
                    break;
                }
                scan_num_bits = nb_bits;
                scan_num_bytes = (nb_bits + 7) / 8;
                scan_bit_index = 0;
                scan_bytes_received = 0;
                scan_bytes_sent = 0;
                memset(scan_tms_buf, 0, sizeof(scan_tms_buf));
                memset(scan_tdi_buf, 0, sizeof(scan_tdi_buf));
                memset(scan_tdo_buf, 0, sizeof(scan_tdo_buf));
                scan_state = SCAN_RECEIVING_TMS;
                break;
            }

            // Full OpenOCD VPI mode: Initialize scan using RX data; reuse legacy scan state machine
            DBG_PRINT(1, "[VPI][DBG] SCAN command: nb_bits=%u, cmd=%u (flip_tms=%d)\n", nb_bits, cmd, (cmd == 3));
            scan_num_bits = nb_bits;
            scan_num_bytes = (nb_bits + 7) / 8;
            scan_bit_index = 0;
            scan_bytes_received = scan_num_bytes; // mark buffers as ready
            scan_bytes_sent = 0;
            scan_is_legacy = false;  // OpenOCD mode - don't send TDO bytes directly
            memset(scan_tdo_buf, 0, sizeof(scan_tdo_buf));
            // For OpenOCD, TMS is 0 for all bits, except last bit when cmd==3
            memset(scan_tms_buf, 0x00, scan_num_bytes);
            if (cmd == 3 && nb_bits > 0) {
                uint32_t last = nb_bits - 1;
                scan_tms_buf[last / 8] |= (1u << (last % 8));
            }
            memcpy(scan_tdi_buf, vpi_cmd_rx.buffer_out, scan_num_bytes);
            // Debug TDI for small scans (likely IR)
            if (scan_num_bytes <= 4) {
                DBG_PRINT(1, "[VPI][DBG] SCAN TDI: ");
                for (uint32_t i = 0; i < scan_num_bytes; i++) {
                    DBG_PRINT(1, "0x%02x ", scan_tdi_buf[i]);
                }
                DBG_PRINT(1, "\n");
            }
            // Enter processing state (legacy engine)
            DBG_PRINT(2, "[VPI][DBG] Entering SCAN_PROCESSING state\n");
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
            // OScan1 SF0 protocol:
            // - Sends TMS on TCKC rising edge (cmd.buffer_out[0] bit 1 = TMS)
            // - Sends TDI on TCKC falling edge (cmd.buffer_out[0] bit 0 = TDI)
            // - Returns captured TDO on TMSC (response.buffer_in[0] = TDO)

            // Extract SF0 control bits from command
            uint8_t tdi = vpi_cmd_rx.buffer_out[0] & 1;      // Bit 0: TDI (falling edge)
            uint8_t tms = (vpi_cmd_rx.buffer_out[0] >> 1) & 1; // Bit 1: TMS (rising edge)

            // Switch to cJTAG two-wire mode
            pending_mode_select = 1;

            // Debug logging for all commands
            DBG_PRINT(1, "[VPI] CMD_OSCAN1: buffer_out[0]=0x%02x → TMS=%d, TDI=%d, current_tdo=%d\n",
                      vpi_cmd_rx.buffer_out[0], tms, tdi, current_tdo);

            // Initialize SF0 state machine for this operation
            // Step 1: Rising edge with TMS bit
            pending_tms = tms;
            pending_tdi = 0;        // During rising edge, TDI is not active
            pending_tckc_toggle = true;  // Create rising edge
            sf0_state = JtagVpiServer::SF0_SEND_TMS;
            sf0_tms = tms;
            sf0_tdi = tdi;
            sf0_tdo = 0;

            DBG_PRINT(1, "[VPI] CMD_OSCAN1: Initializing SF0 state machine (TMS=%d, TDI=%d)\n", tms, tdi);

            // Prepare response packet - will be filled with TDO when complete
            memset(&vpi_cmd_tx, 0, sizeof(vpi_cmd_tx));
            host_to_le32(vpi_cmd_tx.cmd_buf, 5);
            host_to_le32(vpi_cmd_tx.length_buf, 1);
            host_to_le32(vpi_cmd_tx.nb_bits_buf, 2);
            vpi_cmd_tx.buffer_in[0] = 0;  // Will be updated with TDO

            // Queue response
            vpi_tx_pending = false;  // Don't send yet - wait for SF0 to complete
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
            DBG_PRINT(2, "[VPI][DBG] Sent %zd bytes, total=%d/%d\n", sent, vpi_tx_bytes, VPI_PKT_SIZE);
            if (vpi_tx_bytes >= VPI_PKT_SIZE) {
                DBG_PRINT(1, "[VPI][DBG] Response packet sent completely\n");
                vpi_tx_pending = false;
                vpi_tx_bytes = 0;
            }
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            close_connection();
            return;
        }
    }

    // 2) Process OScan1 SF0 state machine (two-phase TCKC/TMSC protocol)
    if (sf0_state != SF0_IDLE) {
        DBG_PRINT(2, "[VPI][DBG] SF0 state machine: state=%d, pending_tckc_toggle=%d, tckc_toggle_consumed=%d, current_tdo=%d\n",
                  sf0_state, pending_tckc_toggle, tckc_toggle_consumed, current_tdo);

        if (sf0_state == SF0_SEND_TMS) {
            // Wait for rising edge to complete (pending_tckc_toggle to be consumed by get_pending_signals)
            DBG_PRINT(2, "[VPI][DBG] SF0_SEND_TMS: pending=%d, consumed=%d\n", pending_tckc_toggle, tckc_toggle_consumed);
            if (pending_tckc_toggle || !tckc_toggle_consumed) {
                DBG_PRINT(2, "[VPI][DBG] SF0_SEND_TMS: Waiting for rising edge to complete\n");
                return;  // Still waiting for toggle to be consumed
            }
            // Rising edge complete - now set up falling edge with TDI
            DBG_PRINT(1, "[VPI][DBG] SF0_SEND_TMS: Rising edge complete, setting up falling edge\n");
            pending_tdi = sf0_tdi;
            pending_tms = 0;      // TMS only on rising edge
            pending_tckc_toggle = true;  // Create falling edge
            tckc_toggle_consumed = false;  // Reset the consumed flag
            sf0_state = SF0_SEND_TDI;
            return;  // Let simulation execute the falling edge
        } else if (sf0_state == SF0_SEND_TDI) {
            // Wait for falling edge to complete (pending_tckc_toggle to be consumed by get_pending_signals)
            DBG_PRINT(2, "[VPI][DBG] SF0_SEND_TDI: pending=%d, consumed=%d\n", pending_tckc_toggle, tckc_toggle_consumed);
            if (pending_tckc_toggle || !tckc_toggle_consumed) {
                DBG_PRINT(2, "[VPI][DBG] SF0_SEND_TDI: Waiting for falling edge to complete\n");
                return;  // Still waiting for toggle to be consumed
            }
            // Both edges complete - capture TDO and queue response
            DBG_PRINT(1, "[VPI][DBG] SF0_SEND_TDI: Falling edge complete, capturing TDO=%d\n", current_tdo);
            sf0_tdo = current_tdo & 1;

            // Update response with captured TDO
            vpi_cmd_tx.buffer_in[0] = sf0_tdo;
            DBG_PRINT(1, "[VPI][DBG] SF0 SF0_SEND_TDI: Queueing response with TDO=0x%02x\n", sf0_tdo);

            // Queue response for transmission
            vpi_tx_pending = true;
            vpi_tx_bytes = 0;
            sf0_state = SF0_IDLE;
            return;  // Response will be sent in next poll
        }
    }

    // 3) Process TMS sequence (no response expected)
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

    // 4) If legacy scan state machine is active, let it progress
    // FIXME BUG FIX: Also handle SCAN_RECEIVING states (not just PROCESSING/SENDING)
    if (scan_state != SCAN_IDLE) {
        DBG_PRINT(2, "[VPI][DBG] continue_vpi_work: scan_state=%d (1=RX_TMS, 2=RX_TDI, 3=PROC, 4=SEND)\n", scan_state);
        // Run legacy per-bit engine
        if (scan_state == SCAN_PROCESSING && pending_tck_pulse) return;
        continue_scan();
        DBG_PRINT(2, "[VPI][DBG] After continue_scan: scan_state=%d, vpi_tx_pending=%d\n",
            scan_state, vpi_tx_pending);
        // When legacy finishes sending TDO bytes, prepare and queue full response
        if (scan_state == SCAN_IDLE && !vpi_tx_pending && client_sock >= 0) {
            DBG_PRINT(2, "[VPI][DBG] Scan complete, preparing response packet\n");
            // Fill TX buffer_in with captured TDO
            memcpy(vpi_cmd_tx.buffer_in, scan_tdo_buf, scan_num_bytes);
            // Debug: Show first few bytes of TDO response
            if (scan_num_bytes >= 4) {
                DBG_PRINT(1, "[VPI][DBG] SCAN response TDO[0-3]=0x%02x 0x%02x 0x%02x 0x%02x\n",
                    scan_tdo_buf[0], scan_tdo_buf[1], scan_tdo_buf[2], scan_tdo_buf[3]);
            } else {
                DBG_PRINT(1, "[VPI][DBG] SCAN response TDO[0]=0x%02x (bytes=%u)\n",
                    scan_tdo_buf[0], scan_num_bytes);
            }
            // Transmit full packet (OpenOCD expects fixed-size)
            vpi_tx_pending = true;
            vpi_tx_bytes = 0;
        }
        return;
    }

    // 4) If idle and not sending, try to receive next command packet
    if (!vpi_tx_pending && client_sock >= 0) {
        // Minimal mode uses a separate 8-byte buffer
        if (vpi_minimal_mode) {
            if (minimal_rx_bytes < sizeof(minimal_cmd_rx)) {
                ssize_t ret = recv(client_sock, ((uint8_t*)&minimal_cmd_rx) + minimal_rx_bytes,
                                   sizeof(minimal_cmd_rx) - minimal_rx_bytes, MSG_DONTWAIT);
                if (ret < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        // Classify different error types
                        const char* error_type = "UNKNOWN";
                        bool should_close = true;

                        switch (errno) {
                            case ECONNRESET:
                                error_type = "CONNECTION_RESET";
                                break;
                            case ENOTCONN:
                                error_type = "NOT_CONNECTED";
                                break;
                            case EINTR:
                                error_type = "INTERRUPTED";
                                should_close = false;  // Recoverable
                                break;
                            case ETIMEDOUT:
                                error_type = "TIMEOUT";
                                should_close = false;  // May be recoverable
                                break;
                            default:
                                error_type = "OTHER";
                                break;
                        }

                        DBG_PRINT(1, "[VPI][WARN] Recv error (%s) in minimal mode: errno=%d, %s%s\n",
                                  error_type, errno, strerror(errno), should_close ? ", closing connection" : ", continuing");

                        if (should_close) {
                            close_connection();
                        }
                    }
                    return;
                }
                if (ret == 0) {
                    DBG_PRINT(1, "[VPI][INFO] Client gracefully disconnected (minimal mode)\n");
                    close_connection();
                    return;
                }
                minimal_rx_bytes += ret;
                DBG_PRINT(2, "[VPI][DBG] Received %zd bytes (minimal), total=%d\n", ret, minimal_rx_bytes);
                if (minimal_rx_bytes < sizeof(minimal_cmd_rx)) {
                    return; // wait for full 8-byte minimal command
                }
            }

            // Have full minimal packet
            process_vpi_packet();
            DBG_PRINT(2, "[VPI][DBG] Minimal packet processed in continue_vpi_work\n");
            minimal_rx_bytes = 0;
            memset(&minimal_cmd_rx, 0, sizeof(minimal_cmd_rx));
            return;
        }

        // Full OpenOCD path: ensure we have at least 8 bytes (already buffered during detection for first packet)
        if (vpi_rx_bytes < 8) {
            ssize_t ret = recv(client_sock, ((uint8_t*)&vpi_cmd_rx) + vpi_rx_bytes,
                               VPI_PKT_SIZE - vpi_rx_bytes, MSG_DONTWAIT);
            if (ret < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    DBG_PRINT(1, "[VPI][DBG] Recv error in continue_vpi_work: %s\n", strerror(errno));
                    close_connection();
                }
                return;
            }
            if (ret == 0) {
                DBG_PRINT(1, "[VPI][DBG] Client disconnected in continue_vpi_work\n");
                close_connection();
                return;
            }
            vpi_rx_bytes += ret;
            DBG_PRINT(2, "[VPI][DBG] Received %zd bytes in continue_vpi_work, total=%d\n", ret, vpi_rx_bytes);
            if (vpi_rx_bytes < 8) {
                return; // wait for minimal command
            }
        }

        // We have at least 8 bytes - check if more data is coming (minimal mode detection for follow-on commands)
        if (vpi_rx_bytes >= 8 && vpi_rx_bytes < VPI_PKT_SIZE) {
            // Peek to see if more data is available
            uint8_t temp_buf[16];
            ssize_t peek_ret = recv(client_sock, temp_buf, sizeof(temp_buf), MSG_DONTWAIT | MSG_PEEK);

            if (vpi_rx_bytes == 8 && peek_ret <= 0 && (errno == EAGAIN || errno == EWOULDBLOCK || peek_ret == 0)) {
                // Exactly 8 bytes, no more data available - minimal mode for next packets
                DBG_PRINT(2, "[VPI][DBG] Minimal mode detected in continue_vpi_work: 8 bytes, no more data\n");
                vpi_minimal_mode = true;
                // Copy header into minimal buffer and process as minimal
                memcpy(&minimal_cmd_rx, &vpi_cmd_rx, sizeof(minimal_cmd_rx));
                minimal_rx_bytes = sizeof(minimal_cmd_rx);
                vpi_rx_bytes = 0;
                memset(&vpi_cmd_rx, 0, sizeof(vpi_cmd_rx));
                process_vpi_packet();
                DBG_PRINT(2, "[VPI][DBG] Minimal packet processed in continue_vpi_work\n");
                minimal_rx_bytes = 0;
                memset(&minimal_cmd_rx, 0, sizeof(minimal_cmd_rx));
                return;
            } else if (peek_ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                // Peek error (not EAGAIN) - connection issue
                DBG_PRINT(1, "[VPI][DBG] Peek error in continue_vpi_work: %s\n", strerror(errno));
                close_connection();
                return;
            }

            // More data available or already buffered - full OpenOCD mode
            vpi_minimal_mode = false;
        }

        // Continue filling until we have full packet (only if NOT in minimal mode)
        if (!vpi_minimal_mode && vpi_rx_bytes < VPI_PKT_SIZE) {
            ssize_t ret = recv(client_sock, ((uint8_t*)&vpi_cmd_rx) + vpi_rx_bytes,
                               VPI_PKT_SIZE - vpi_rx_bytes, MSG_DONTWAIT);
            if (ret < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    // Classify different error types
                    const char* error_type = "UNKNOWN";
                    bool should_close = true;

                    switch (errno) {
                        case ECONNRESET:
                            error_type = "CONNECTION_RESET";
                            break;
                        case ENOTCONN:
                            error_type = "NOT_CONNECTED";
                            break;
                        case EINTR:
                            error_type = "INTERRUPTED";
                            should_close = false;  // Recoverable
                            break;
                        case ETIMEDOUT:
                            error_type = "TIMEOUT";
                            should_close = false;  // May be recoverable
                            break;
                        default:
                            error_type = "OTHER";
                            break;
                    }

                    DBG_PRINT(1, "[VPI][WARN] Recv error (%s) in full packet mode: errno=%d, %s, rx_bytes=%d/%d%s\n",
                              error_type, errno, strerror(errno), vpi_rx_bytes, VPI_PKT_SIZE,
                              should_close ? ", closing connection" : ", continuing");

                    if (should_close) {
                        close_connection();
                    }
                }
                return;
            }
            if (ret == 0) {
                DBG_PRINT(1, "[VPI][INFO] Client gracefully disconnected (rx_bytes=%d/%d)\n", vpi_rx_bytes, VPI_PKT_SIZE);
                close_connection();
                return;
            }
            vpi_rx_bytes += ret;
            DBG_PRINT(2, "[VPI][DBG] Received %zd bytes in continue_vpi_work, total=%d\n", ret, vpi_rx_bytes);
            if (vpi_rx_bytes < VPI_PKT_SIZE) {
                return; // wait for rest of packet
            }
        }

        // Full packet received - process it
        DBG_PRINT(2, "[VPI][DBG] Full packet received in continue_vpi_work, processing...\n");
        process_vpi_packet();
        DBG_PRINT(2, "[VPI][DBG] Packet processed in continue_vpi_work, resetting buffer\n");
        vpi_rx_bytes = 0;
        memset(&vpi_cmd_rx, 0, sizeof(vpi_cmd_rx));
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

    // Send response back to client (non-blocking with timeout)
    if (send_resp && client_sock >= 0) {
        ssize_t ret = send(client_sock, resp, sizeof(*resp), MSG_DONTWAIT);
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full - try again after a small delay
                usleep(1000); // 1ms delay
                ret = send(client_sock, resp, sizeof(*resp), MSG_DONTWAIT);
                if (ret < 0) {
                    DBG_PRINT(1, "[VPI][DBG] Send retry failed: %s, closing connection\n", strerror(errno));
                    close_connection();
                }
            } else {
                DBG_PRINT(1, "[VPI][DBG] Send error: %s, closing connection\n", strerror(errno));
                close_connection();
            }
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
    scan_is_legacy = true;  // Legacy protocol mode
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

            DBG_PRINT(2, "[VPI][DBG] SCAN_PROCESSING: bit_index=%u/%u, pending_tck=%d\n",
                scan_bit_index, scan_num_bits, pending_tck_pulse);

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
                DBG_PRINT(2, "[VPI][DBG] SCAN_PROCESSING complete: %u bits processed\n", scan_bit_index);

                if (scan_is_legacy) {
                    // Legacy protocol: Send TDO bytes directly over socket
                    scan_bytes_sent = 0;
                    scan_state = SCAN_SENDING_TDO;
                } else {
                    // OpenOCD VPI: Don't send bytes here, go to IDLE
                    // continue_vpi_work() will prepare and send full 1036-byte response
                    scan_state = SCAN_IDLE;
                }
            }
            break;

        case SCAN_SENDING_TDO:
            DBG_PRINT(2, "[VPI][DBG] SCAN_SENDING_TDO: %u/%u bytes sent\n", scan_bytes_sent, scan_num_bytes);
            // Send TDO buffer as response packets
            // Send up to all bytes in one go since non-blocking might handle it
            ret = send(client_sock, scan_tdo_buf + scan_bytes_sent,
                      scan_num_bytes - scan_bytes_sent, MSG_DONTWAIT);
            if (ret > 0) {
                scan_bytes_sent += ret;
                if (scan_bytes_sent >= scan_num_bytes) {
                    DBG_PRINT(2, "[VPI][DBG] SCAN_SENDING_TDO complete: %u bytes sent\n", scan_bytes_sent);
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
                // Classify different error types
                const char* error_type = "UNKNOWN";
                bool should_close = true;

                switch (errno) {
                    case EPIPE:
                        error_type = "BROKEN_PIPE";
                        break;
                    case ECONNRESET:
                        error_type = "CONNECTION_RESET";
                        break;
                    case ENOTCONN:
                        error_type = "NOT_CONNECTED";
                        break;
                    case EINTR:
                        error_type = "INTERRUPTED";
                        should_close = false;  // Recoverable
                        break;
                    default:
                        error_type = "OTHER";
                        break;
                }

                DBG_PRINT(1, "[VPI][WARN] TDO send error (%s) during SCAN: errno=%d, %s, sent=%u/%u bytes%s\n",
                          error_type, errno, strerror(errno), scan_bytes_sent, scan_num_bytes,
                          should_close ? ", closing connection" : ", retrying");

                if (should_close) {
                    scan_state = SCAN_IDLE;
                    close_connection();
                } else {
                    // For recoverable errors, don't change scan state and try again later
                    usleep(1000); // 1ms delay
                }
            }
            break;

        default:
            scan_state = SCAN_IDLE;
            break;
    }
}

void JtagVpiServer::close_connection() {
    DBG_PRINT(1, "[VPI][INFO] Closing connection (socket=%d, protocol=%s, rx_bytes=%d, scan_state=%d, tx_pending=%s)\n",
              client_sock,
              (protocol_mode == PROTO_OPENOCD_VPI) ? "OpenOCD" : (protocol_mode == PROTO_UNKNOWN) ? "Unknown" : "Legacy",
              vpi_rx_bytes, scan_state, vpi_tx_pending ? "true" : "false");

    if (client_sock >= 0) {
        // Try to get socket error status before closing
        int socket_error = 0;
        socklen_t len = sizeof(socket_error);
        if (getsockopt(client_sock, SOL_SOCKET, SO_ERROR, &socket_error, &len) == 0 && socket_error != 0) {
            DBG_PRINT(1, "[VPI][INFO] Socket error status: %s\n", strerror(socket_error));
        }

        close(client_sock);
        client_sock = -1;
    }

    // Reset protocol mode so next client can be detected correctly
    protocol_mode = PROTO_UNKNOWN;
    vpi_rx_bytes = 0;
    vpi_tx_pending = false;
    vpi_minimal_mode = false;
    // Reset scan state machine
    scan_state = SCAN_IDLE;
    // Reset TMS sequence state
    tms_seq_active = false;

    DBG_PRINT(1, "[VPI][INFO] Connection cleanup complete, ready for new client\n");
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

bool JtagVpiServer::get_pending_signals(uint8_t* tms, uint8_t* tdi, uint8_t* mode_sel, bool* tck_pulse, bool* tckc_toggle) {
    // Handle queued reset pulses first
    if (reset_pulses_remaining > 0) {
        *tms = 1;
        *tdi = 0;
        *mode_sel = pending_mode_select;
        *tck_pulse = true;
        if (tckc_toggle) *tckc_toggle = false;
        reset_pulses_remaining--;
        return true;
    }

    // Check if there's any pending signal change
    bool has_signal_change = (pending_tck_pulse || pending_tckc_toggle);
    bool has_mode_change = (pending_mode_select != current_mode);

    if (!has_signal_change && !has_mode_change) {
        return false;
    }

    *tms = pending_tms;
    *tdi = pending_tdi;
    *mode_sel = pending_mode_select;  // Always return current mode setting
    *tck_pulse = pending_tck_pulse;
    if (tckc_toggle) *tckc_toggle = pending_tckc_toggle;

    pending_tck_pulse = false;
    if (pending_tckc_toggle) {
        tckc_toggle_consumed = true;  // Mark that we just consumed a TCKC toggle
    }
    pending_tckc_toggle = false;

    return true;
}

void JtagVpiServer::set_mode(uint8_t mode) {
    pending_mode_select = mode;
    DBG_PRINT(1, "[VPI] Initial mode set to: %s\n", mode ? "cJTAG" : "JTAG");
}
