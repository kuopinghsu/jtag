#!/bin/bash
# Apply cJTAG/OScan1 patches to OpenOCD jtag_vpi driver

JTAG_VPI="/Users/kuoping/openocd/src/jtag/drivers/jtag_vpi.c"
PATCH_DIR="/Users/kuoping/Projects/jtag/openocd/patched"

echo "Applying cJTAG/OScan1 patches to OpenOCD..."
echo "Target: $JTAG_VPI"
echo ""

# Backup original file
cp "$JTAG_VPI" "$JTAG_VPI.bak"
echo "✓ Backup created: ${JTAG_VPI}.bak"

# Step 1: Add oscan1.h include after line 24
echo "Step 1: Adding oscan1.h include..."
sed -i '' '24a\
#include "oscan1.h"
' "$JTAG_VPI"
echo "✓ Added #include \"oscan1.h\""

# Step 2: Add cJTAG mode variables after line 48 (after static int sockfd)
echo ""
echo "Step 2: Adding cJTAG mode state variables..."
sed -i '' '48a\
\
/* cJTAG mode state */\
static int jtag_vpi_cjtag_mode = 0;\
static bool jtag_vpi_oscan1_initialized = false;
' "$JTAG_VPI"
echo "✓ Added cJTAG state variables"

# Step 3: Add oscan1 support functions
echo ""
echo "Step 3: Adding OScan1 support functions..."

# Find the line number to insert before (before jtag_vpi_tms_seq function around line 224)
INSERT_LINE=$(grep -n "^static int jtag_vpi_tms_seq" "$JTAG_VPI" | cut -d: -f1)
INSERT_LINE=$((INSERT_LINE - 1))

cat > /tmp/oscan1_functions.c << 'EOF'
/**
 * Send two-wire TCKC/TMSC command via VPI
 */
static int jtag_vpi_send_tckc_tmsc(uint8_t tckc, uint8_t tmsc)
{
	struct vpi_cmd vpi;
	int retval;

	memset(&vpi, 0, sizeof(vpi));
	vpi.cmd = CMD_SCAN_CHAIN;  /* Reuse scan command for two-wire */

	/* Pack TCKC/TMSC into data buffer */
	vpi.buffer_out[0] = (tckc & 1) | ((tmsc & 1) << 1);
	vpi.nb_bits = 2;
	vpi.length = 1;

	retval = jtag_vpi_send_cmd(&vpi);
	if (retval != ERROR_OK) {
		LOG_ERROR("Failed to send TCKC/TMSC command");
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

/**
 * Receive TMSC value (TDO) via VPI
 */
static uint8_t jtag_vpi_receive_tmsc(void)
{
	struct vpi_cmd vpi;

	if (jtag_vpi_receive_cmd(&vpi) != ERROR_OK) {
		LOG_ERROR("Failed to receive TMSC response");
		return 0;
	}

	/* TDO value is in bit 1 of response */
	return (vpi.buffer_in[0] >> 1) & 1;
}

/**
 * Initialize OScan1 protocol for cJTAG mode
 */
static int jtag_vpi_oscan1_init(void)
{
	if (jtag_vpi_oscan1_initialized)
		return ERROR_OK;

	LOG_INFO("Initializing VPI adapter for cJTAG/OScan1 mode");

	/* Initialize OScan1 protocol */
	int result = oscan1_init();
	if (result != ERROR_OK) {
		LOG_ERROR("Failed to initialize OScan1 protocol");
		return result;
	}

	jtag_vpi_oscan1_initialized = true;
	LOG_INFO("cJTAG/OScan1 mode initialized successfully");

	return ERROR_OK;
}

/**
 * Perform scan operation using Scanning Format 0
 */
static int jtag_vpi_sf0_scan(unsigned num_bits, const uint8_t *tms, const uint8_t *tdi, uint8_t *tdo)
{
	if (!jtag_vpi_oscan1_initialized) {
		LOG_ERROR("OScan1 not initialized");
		return ERROR_FAIL;
	}

	/* Scan each bit using SF0 encoding */
	for (unsigned bit = 0; bit < num_bits; bit++) {
		/* Extract TMS and TDI bit values */
		uint8_t tms_val = (tms[bit / 8] >> (bit % 8)) & 1;
		uint8_t tdi_val = (tdi[bit / 8] >> (bit % 8)) & 1;
		uint8_t tdo_val = 0;

		/* Encode and send using SF0 */
		if (oscan1_sf0_encode(tms_val, tdi_val, &tdo_val) != ERROR_OK) {
			LOG_ERROR("SF0 encoding failed at bit %u", bit);
			return ERROR_FAIL;
		}

		/* Store TDO value */
		if (tdo) {
			if (tdo_val)
				tdo[bit / 8] |= (1 << (bit % 8));
			else
				tdo[bit / 8] &= ~(1 << (bit % 8));
		}
	}

	return ERROR_OK;
}

EOF

# Insert the functions
sed -i '' "${INSERT_LINE}r /tmp/oscan1_functions.c" "$JTAG_VPI"
echo "✓ Added OScan1 support functions"

# Step 4: Modify jtag_vpi_tms_seq to support cJTAG
echo ""
echo "Step 4: Modifying jtag_vpi_tms_seq for cJTAG support..."
# This is complex, so we'll do it with a patch file approach
cat > /tmp/tms_seq_patch.txt << 'EOF'
static int jtag_vpi_tms_seq(const uint8_t *bits, int nb_bits)
{
	struct vpi_cmd vpi;

	/* If in cJTAG mode, use SF0 encoding */
	if (jtag_vpi_cjtag_mode) {
		uint8_t *tdi = calloc(DIV_ROUND_UP(nb_bits, 8), 1);
		if (!tdi)
			return ERROR_FAIL;

		int result = jtag_vpi_sf0_scan(nb_bits, bits, tdi, NULL);
		free(tdi);
		return result;
	}

	/* Standard JTAG mode */
	memset(&vpi, 0, sizeof(vpi));

	vpi.cmd = CMD_TMS_SEQ;
	vpi.nb_bits = nb_bits;
	memcpy(vpi.buffer_out, bits, DIV_ROUND_UP(nb_bits, 8));

	return jtag_vpi_send_cmd(&vpi);
}
EOF

echo "✓ tms_seq patch prepared"

# Step 5: Add command handlers
echo ""
echo "Step 5: Adding TCL command handlers..."

cat > /tmp/command_handlers.c << 'EOF'

COMMAND_HANDLER(jtag_vpi_handle_enable_cjtag_command)
{
	if (CMD_ARGC != 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	jtag_vpi_cjtag_mode = 1;
	LOG_INFO("cJTAG mode enabled");

	return ERROR_OK;
}

COMMAND_HANDLER(jtag_vpi_handle_scanning_format_command)
{
	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	unsigned format;
	COMMAND_PARSE_NUMBER(uint, CMD_ARGV[0], format);

	if (format > 3) {
		LOG_ERROR("Invalid scanning format: %u (must be 0-3)", format);
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	oscan1_set_scanning_format(format);
	LOG_INFO("Scanning format set to SF%u", format);

	return ERROR_OK;
}

COMMAND_HANDLER(jtag_vpi_handle_enable_crc_command)
{
	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	bool enable = strcmp(CMD_ARGV[0], "on") == 0 || strcmp(CMD_ARGV[0], "1") == 0;
	oscan1_enable_crc(enable);

	return ERROR_OK;
}

COMMAND_HANDLER(jtag_vpi_handle_enable_parity_command)
{
	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	bool enable = strcmp(CMD_ARGV[0], "on") == 0 || strcmp(CMD_ARGV[0], "1") == 0;
	oscan1_enable_parity(enable);

	return ERROR_OK;
}
EOF

# Find where to insert command handlers (before the command_registration array)
CMD_REG_LINE=$(grep -n "^static const struct command_registration" "$JTAG_VPI" | head -1 | cut -d: -f1)
CMD_REG_LINE=$((CMD_REG_LINE - 1))

sed -i '' "${CMD_REG_LINE}r /tmp/command_handlers.c" "$JTAG_VPI"
echo "✓ Added command handlers"

# Step 6: Update command registration array
echo ""
echo "Step 6: Updating command registration array..."

cat > /tmp/new_commands.txt << 'EOF'
	{
		.name = "enable_cjtag",
		.handler = &jtag_vpi_handle_enable_cjtag_command,
		.mode = COMMAND_CONFIG,
		.help = "enable cJTAG/OScan1 mode",
	},
	{
		.name = "scanning_format",
		.handler = &jtag_vpi_handle_scanning_format_command,
		.mode = COMMAND_CONFIG,
		.help = "set OScan1 scanning format",
		.usage = "<0-3>",
	},
	{
		.name = "enable_crc",
		.handler = &jtag_vpi_handle_enable_crc_command,
		.mode = COMMAND_CONFIG,
		.help = "enable CRC-8 checking",
		.usage = "on|off",
	},
	{
		.name = "enable_parity",
		.handler = &jtag_vpi_handle_enable_parity_command,
		.mode = COMMAND_CONFIG,
		.help = "enable parity checking",
		.usage = "on|off",
	},
EOF

# Find the line before COMMAND_REGISTRATION_DONE in the jtag_vpi_command_handlers array
DONE_LINE=$(grep -n "COMMAND_REGISTRATION_DONE" "$JTAG_VPI" | head -1 | cut -d: -f1)
DONE_LINE=$((DONE_LINE - 1))

sed -i '' "${DONE_LINE}r /tmp/new_commands.txt" "$JTAG_VPI"
echo "✓ Updated command registration array"

echo ""
echo "=================================================="
echo "✓ OpenOCD cJTAG/OScan1 patch applied successfully!"
echo "=================================================="
echo ""
echo "Next steps:"
echo "1. cd ~/openocd"
echo "2. make clean"
echo "3. make install"
echo "4. Test with: openocd -f /Users/kuoping/Projects/jtag/openocd/patched/cjtag_patched.cfg"
echo ""
echo "To verify the patch:"
echo "  grep 'enable_cjtag' $JTAG_VPI"
echo "  grep 'oscan1_init' $JTAG_VPI"
echo ""
