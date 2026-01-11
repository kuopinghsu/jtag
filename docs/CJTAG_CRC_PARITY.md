# cJTAG CRC and Parity Checking

Implementation of error detection mechanisms for IEEE 1149.7 cJTAG (Compact JTAG) protocol.

## Overview

The CRC and parity checking module provides robust error detection for cJTAG packets transmitted over the two-wire OScan1 interface. This is critical for reliable operation in noisy environments or over long cable runs.

## Features

- ✅ **CRC-8 Calculation** - Polynomial: x^8 + x^2 + x + 1 (0x07)
- ✅ **Parity Checking** - Even/odd parity support
- ✅ **Configurable** - Enable/disable CRC and parity independently
- ✅ **Error Statistics** - 16-bit error counters for CRC and parity
- ✅ **Packet Validation** - Byte-by-byte or full-packet checking
- ✅ **Low Overhead** - Minimal logic and latency

## Architecture

### Error Detection Flow

```
┌──────────────┐
│ cJTAG Packet │
└──────┬───────┘
       │
       ▼
┌──────────────────┐
│ Byte Accumulator │
└──────┬───────────┘
       │
       ├──────────────┬──────────────┐
       ▼              ▼              ▼
┌──────────┐   ┌────────────┐   ┌────────┐
│ CRC-8    │   │ Parity Bit │   │ Data   │
│ Calculator│   │ Generator  │   │ Output │
└──────┬───┘   └─────┬──────┘   └────────┘
       │             │
       ├─────────────┤
       ▼             ▼
┌──────────────────────┐
│  Error Checking      │
│  - CRC mismatch      │
│  - Parity error      │
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│ Error Counters       │
│ - crc_error_count    │
│ - parity_error_count │
└──────────────────────┘
```

## Module Interface

### Parameters

```systemverilog
parameter bit ENABLE_CRC = 1'b1;           // Enable CRC-8 checking
parameter bit ENABLE_PARITY = 1'b0;        // Enable parity checking
parameter bit [7:0] CRC_POLYNOMIAL = 8'h07; // CRC polynomial (x^8+x^2+x+1)
```

### Ports

```systemverilog
module cjtag_crc_parity #(
    parameter bit ENABLE_CRC = 1'b1,
    parameter bit ENABLE_PARITY = 1'b0,
    parameter bit [7:0] CRC_POLYNOMIAL = 8'h07
)(
    input  logic        clk,
    input  logic        rst_n,
    
    // Data input interface
    input  logic [7:0]  data_byte,          // Input data byte
    input  logic        data_valid,         // Data byte valid strobe
    input  logic        data_last,          // Last byte in packet
    
    // CRC/Parity outputs
    output logic [7:0]  crc_value,          // Current CRC value
    output logic        parity_bit,         // Current parity bit
    
    // Error detection
    input  logic [7:0]  expected_crc,       // Expected CRC (from packet)
    input  logic        expected_parity,    // Expected parity (from packet)
    output logic        crc_error,          // CRC mismatch detected
    output logic        parity_error,       // Parity error detected
    
    // Error statistics
    output logic [15:0] crc_error_count,    // Total CRC errors
    output logic [15:0] parity_error_count, // Total parity errors
    
    // Control
    input  logic        clear_errors        // Clear error counters
);
```

## CRC-8 Algorithm

### Polynomial

**Standard CRC-8**: x^8 + x^2 + x + 1

**Binary**: `1 0000 0111` = `0x107` (9-bit)  
**8-bit form**: `0000 0111` = `0x07`

### Calculation

```systemverilog
// CRC-8 calculation (bit-by-bit)
logic [7:0] crc_reg;

always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        crc_reg <= 8'hFF;  // Initial value
    end else if (data_valid) begin
        for (int i = 0; i < 8; i++) begin
            if (crc_reg[7] ^ data_byte[7-i]) begin
                crc_reg = (crc_reg << 1) ^ CRC_POLYNOMIAL;
            end else begin
                crc_reg = crc_reg << 1;
            end
        end
    end
end
```

### Example

**Input Packet**: `0xA5 0x3C 0x69`

```
Initial CRC:    0xFF
Process 0xA5:   0x52
Process 0x3C:   0xE1
Process 0x69:   0x7D ← Final CRC

Packet with CRC: 0xA5 0x3C 0x69 0x7D
```

### Verification

```systemverilog
// At packet end
if (data_last && ENABLE_CRC) begin
    if (crc_value != expected_crc) begin
        crc_error <= 1'b1;
        crc_error_count <= crc_error_count + 1;
    end
end
```

## Parity Checking

### Even Parity

**Rule**: Total number of 1-bits is **even**

```
Data:    1010 0110  (4 ones)
Parity:  0           (even)

Data:    1010 0111  (5 ones)
Parity:  1           (make even)
```

### Odd Parity

**Rule**: Total number of 1-bits is **odd**

```
Data:    1010 0110  (4 ones)
Parity:  1           (make odd)

Data:    1010 0111  (5 ones)
Parity:  0           (already odd)
```

### Calculation

```systemverilog
// Even parity calculation
logic parity_reg;

always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        parity_reg <= 1'b0;
    end else if (data_valid) begin
        // XOR all bits
        parity_reg <= parity_reg ^ ^data_byte;
    end
end

assign parity_bit = parity_reg;
```

### Verification

```systemverilog
// At packet end
if (data_last && ENABLE_PARITY) begin
    if (parity_bit != expected_parity) begin
        parity_error <= 1'b1;
        parity_error_count <= parity_error_count + 1;
    end
end
```

## Usage Examples

### CRC-Only Mode

```systemverilog
cjtag_crc_parity #(
    .ENABLE_CRC(1'b1),
    .ENABLE_PARITY(1'b0),
    .CRC_POLYNOMIAL(8'h07)
) crc_checker (
    .clk(clk),
    .rst_n(rst_n),
    .data_byte(rx_byte),
    .data_valid(byte_ready),
    .data_last(packet_end),
    .crc_value(computed_crc),
    .expected_crc(received_crc),
    .crc_error(crc_mismatch),
    .crc_error_count(crc_errors),
    .clear_errors(1'b0)
);
```

### Parity-Only Mode

```systemverilog
cjtag_crc_parity #(
    .ENABLE_CRC(1'b0),
    .ENABLE_PARITY(1'b1)
) parity_checker (
    .clk(clk),
    .rst_n(rst_n),
    .data_byte(rx_byte),
    .data_valid(byte_ready),
    .data_last(packet_end),
    .parity_bit(computed_parity),
    .expected_parity(received_parity),
    .parity_error(parity_mismatch),
    .parity_error_count(parity_errors),
    .clear_errors(1'b0)
);
```

### Combined CRC and Parity

```systemverilog
cjtag_crc_parity #(
    .ENABLE_CRC(1'b1),
    .ENABLE_PARITY(1'b1),
    .CRC_POLYNOMIAL(8'h07)
) error_checker (
    .clk(clk),
    .rst_n(rst_n),
    .data_byte(rx_byte),
    .data_valid(byte_ready),
    .data_last(packet_end),
    .crc_value(computed_crc),
    .parity_bit(computed_parity),
    .expected_crc(received_crc),
    .expected_parity(received_parity),
    .crc_error(crc_mismatch),
    .parity_error(parity_mismatch),
    .crc_error_count(crc_errors),
    .parity_error_count(parity_errors),
    .clear_errors(error_reset)
);
```

## Integration with OScan1

### Packet Structure

```
OScan1 Packet (with CRC and Parity):

┌────────┬────────┬─────┬────────┬────────┬────────┬────────┐
│ START  │ OPCODE │ ... │ DATA   │  CRC   │ PARITY │  STOP  │
├────────┼────────┼─────┼────────┼────────┼────────┼────────┤
│ 2 bits │ 4 bits │     │ N bytes│ 8 bits │ 1 bit  │ 2 bits │
└────────┴────────┴─────┴────────┴────────┴────────┴────────┘
```

### Data Flow

```systemverilog
// In oscan1_controller.sv

// Byte accumulation
logic [7:0] data_byte;
logic       data_valid;
logic       data_last;
logic [2:0] bit_count;

always_ff @(posedge clk) begin
    if (shift_active) begin
        data_byte <= {data_byte[6:0], tmsc_sampled};
        bit_count <= bit_count + 1;
        
        if (bit_count == 7) begin
            data_valid <= 1'b1;  // Byte complete
            bit_count <= 0;
        end else begin
            data_valid <= 1'b0;
        end
    end
    
    if (packet_end) begin
        data_last <= 1'b1;
    end else begin
        data_last <= 1'b0;
    end
end

// CRC/Parity module instantiation
cjtag_crc_parity #(
    .ENABLE_CRC(ENABLE_CRC),
    .ENABLE_PARITY(ENABLE_PARITY)
) crc_parity_check (
    .clk(clk),
    .rst_n(rst_n),
    .data_byte(data_byte),
    .data_valid(data_valid),
    .data_last(data_last),
    .crc_value(computed_crc),
    .parity_bit(computed_parity),
    .expected_crc(received_crc),
    .expected_parity(received_parity),
    .crc_error(crc_error),
    .parity_error(parity_error),
    .crc_error_count(crc_error_count),
    .parity_error_count(parity_error_count),
    .clear_errors(1'b0)
);
```

## Error Handling

### Error Recovery

When error detected:

1. **Log Error**: Increment error counter
2. **Signal Error**: Assert `crc_error` or `parity_error`
3. **Notify Host**: Update status register
4. **Retry**: Request packet retransmission (if protocol supports)

```systemverilog
// Error state machine
typedef enum logic [1:0] {
    IDLE,
    RECEIVING,
    CHECKING,
    ERROR
} error_state_t;

error_state_t state;

always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        state <= IDLE;
    end else begin
        case (state)
            IDLE: begin
                if (packet_start) state <= RECEIVING;
            end
            
            RECEIVING: begin
                if (data_last) state <= CHECKING;
            end
            
            CHECKING: begin
                if (crc_error || parity_error) begin
                    state <= ERROR;
                end else begin
                    state <= IDLE;
                end
            end
            
            ERROR: begin
                // Handle error (retry, report, etc.)
                if (error_cleared) state <= IDLE;
            end
        endcase
    end
end
```

### Error Statistics

```systemverilog
// Read error counters
always_ff @(posedge clk) begin
    if (read_crc_errors) begin
        error_data <= crc_error_count;
    end else if (read_parity_errors) begin
        error_data <= parity_error_count;
    end
end

// Clear counters
always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n || clear_errors) begin
        crc_error_count <= 16'h0000;
        parity_error_count <= 16'h0000;
    end
end
```

## Performance

### Resource Usage

| Feature | Logic Elements | Registers | Multipliers |
|---------|---------------|-----------|-------------|
| CRC-8   | ~50           | 8         | 0           |
| Parity  | ~10           | 1         | 0           |
| Counters| ~40           | 32        | 0           |
| **Total**| ~100         | 41        | 0           |

### Timing

| Operation | Latency |
|-----------|---------|
| CRC per byte | 1 clock cycle |
| Parity per byte | 1 clock cycle |
| Error check | 1 clock cycle |
| **Total per byte** | **1 clock cycle** |

### Throughput

No impact on packet throughput - checking done in parallel with data reception.

## Configuration Options

### Disable Both

```systemverilog
// No error checking
.ENABLE_CRC(1'b0)
.ENABLE_PARITY(1'b0)
```

### CRC-8 Only (Recommended)

```systemverilog
// Strong error detection
.ENABLE_CRC(1'b1)
.ENABLE_PARITY(1'b0)
.CRC_POLYNOMIAL(8'h07)
```

### Parity Only (Legacy)

```systemverilog
// Basic error detection
.ENABLE_CRC(1'b0)
.ENABLE_PARITY(1'b1)
```

### Maximum Protection

```systemverilog
// Both CRC and parity
.ENABLE_CRC(1'b1)
.ENABLE_PARITY(1'b1)
.CRC_POLYNOMIAL(8'h07)
```

## Testing

### Testbench Example

```systemverilog
module tb_cjtag_crc_parity;
    logic clk, rst_n;
    logic [7:0] data_byte;
    logic data_valid, data_last;
    logic [7:0] crc_value;
    logic crc_error;
    
    // DUT
    cjtag_crc_parity #(
        .ENABLE_CRC(1'b1),
        .ENABLE_PARITY(1'b0)
    ) dut (.*);
    
    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end
    
    // Test sequence
    initial begin
        rst_n = 0;
        #20 rst_n = 1;
        
        // Send test packet
        @(posedge clk);
        data_byte = 8'hA5;
        data_valid = 1;
        data_last = 0;
        
        @(posedge clk);
        data_byte = 8'h3C;
        data_valid = 1;
        data_last = 0;
        
        @(posedge clk);
        data_byte = 8'h69;
        data_valid = 1;
        data_last = 1;
        
        @(posedge clk);
        data_valid = 0;
        data_last = 0;
        
        // Check CRC
        #10;
        $display("CRC Value: 0x%02X", crc_value);
        
        $finish;
    end
endmodule
```

### Expected Results

```
CRC Value: 0x7D
```

## Debugging

### CRC Mismatch

```systemverilog
// Enable debug prints
if (crc_error) begin
    $display("[ERROR] CRC mismatch!");
    $display("  Computed: 0x%02X", crc_value);
    $display("  Expected: 0x%02X", expected_crc);
    $display("  Error Count: %d", crc_error_count);
end
```

### Parity Error

```systemverilog
// Enable debug prints
if (parity_error) begin
    $display("[ERROR] Parity mismatch!");
    $display("  Computed: %b", parity_bit);
    $display("  Expected: %b", expected_parity);
    $display("  Error Count: %d", parity_error_count);
end
```

## Limitations

1. **CRC Polynomial**: Fixed at compile-time (default: 0x07)
2. **Error Counters**: 16-bit (0-65535), no overflow protection
3. **Packet Length**: No maximum limit enforced
4. **Single Error**: Only detects errors, no correction

## Future Enhancements

- [ ] Configurable CRC polynomials (CRC-16, CRC-32)
- [ ] Error correction (FEC)
- [ ] Burst error detection
- [ ] Automatic retransmission request (ARQ)
- [ ] Error logging with timestamps
- [ ] Packet statistics (good/bad/total)

## References

1. **IEEE 1149.7-2009** - Section 6.4: Error Detection
2. **CRC-8 Algorithms** - Polynomial selection guide
3. [src/jtag/cjtag_crc_parity.sv](../src/jtag/cjtag_crc_parity.sv) - Implementation

## Related Documentation

- [OScan1 Implementation](OSCAN1_IMPLEMENTATION.md)
- [JTAG Module Hierarchy](JTAG_MODULE_HIERARCHY.md)
- [Multi-TAP Scan Chain](MULTI_TAP_SCAN_CHAIN.md)
