#!/usr/bin/env python3
"""
Simple VPI client for testing the JTAG VPI server
"""

import socket
import struct
import time
import sys

def send_reset_command(sock):
    """Send CMD_RESET command"""
    cmd = struct.pack('!BBBxI', 0x00, 0, 0, 0)  # cmd=0, pad, length=0
    print(f"[Client] Sending CMD_RESET: {cmd.hex()}")
    sock.sendall(cmd)
    
    # Read response
    resp = sock.recv(4)
    if resp:
        print(f"[Client] Received response: {resp.hex()}")
        response, tdo, mode, status = struct.unpack('!BBBB', resp)
        print(f"[Client]   response={response}, tdo={tdo}, mode={mode}, status={status}")
        return True
    return False

def send_scan_command(sock, num_bits):
    """Send CMD_SCAN command"""
    num_bytes = (num_bits + 7) // 8
    cmd = struct.pack('!BBBxI', 0x02, 0, 0, num_bits)  # cmd=2, pad, length=num_bits
    print(f"[Client] Sending CMD_SCAN for {num_bits} bits: {cmd.hex()}")
    sock.sendall(cmd)
    
    time.sleep(0.1)  # Wait for server to acknowledge
    
    # Send TMS buffer (all zeros - go to Shift-IR state)
    tms_buf = bytes(num_bytes)
    print(f"[Client] Sending TMS buffer ({num_bytes} bytes): {tms_buf.hex()}")
    sock.sendall(tms_buf)
    
    time.sleep(0.1)
    
    # Send TDI buffer (all zeros)
    tdi_buf = bytes(num_bytes)
    print(f"[Client] Sending TDI buffer ({num_bytes} bytes): {tdi_buf.hex()}")
    sock.sendall(tdi_buf)
    
    time.sleep(0.1)
    
    # Read TDO response
    tdo_buf = sock.recv(num_bytes)
    if tdo_buf:
        print(f"[Client] Received TDO buffer ({len(tdo_buf)} bytes): {tdo_buf.hex()}")
        return True
    return False

def main():
    # Connect to VPI server
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        print("[Client] Connecting to 127.0.0.1:3333...")
        sock.connect(('127.0.0.1', 3333))
        print("[Client] Connected!")
        
        # Send reset
        print("\n[Step 1] Reset TAP...")
        send_reset_command(sock)
        time.sleep(0.5)
        
        # Send scan for 32 bits
        print("\n[Step 2] Scan 32 bits...")
        send_scan_command(sock, 32)
        time.sleep(0.5)
        
        print("\n[Test Complete]")
        
    except Exception as e:
        print(f"[Error] {e}")
        sys.exit(1)
    finally:
        sock.close()

if __name__ == '__main__':
    main()
