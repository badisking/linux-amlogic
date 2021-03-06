/*
 * drivers/amlogic/display/vout/lcd/mipi_dsi_util.c
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
*/

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/amlogic/vout/lcdoutc.h>
#ifdef CONFIG_AML_LCD_EXTERN
#include <linux/amlogic/vout/lcd_extern.h>
#endif
#include "lcd_reg.h"
#include "mipi_dsi_reg.h"
#include "lcd_control.h"
#include "mipi_dsi_util.h"

/* *************************************************************
 * Define MIPI DSI Default config
 * ************************************************************* */
/* Range [0,3] */
#define MIPI_DSI_VIRTUAL_CHAN_ID        0
/* Define DSI command transfer type: high speed or low power */
#define MIPI_DSI_CMD_TRANS_TYPE         DCS_TRANS_LP
/* Define if DSI command need ack: req_ack or no_ack */
#define MIPI_DSI_DCS_ACK_TYPE           MIPI_DSI_DCS_NO_ACK
/* Applicable only to video mode. Define data transfer method:
      non-burst sync pulse; non-burst sync event; or burst. */
#define MIPI_DSI_VIDEO_MODE_TYPE        BURST_MODE
#define MIPI_DSI_COLOR_18BIT            COLOR_18BIT_CFG_1
#define MIPI_DSI_COLOR_24BIT            COLOR_24BIT
#define MIPI_DSI_TEAR_SWITCH            MIPI_DCS_DISABLE_TEAR
#define CMD_TIMEOUT_CNT                 3000
/* ************************************************************* */

static char *video_mode_type[] = {
	"NON_BURST_SYNC_PULSE",
	"NON_BURST_SYNC_EVENT",
	"BURST",
};

static char *video_data_type_table[] = {
	"COLOR_16BIT_CFG_1",
	"COLOR_16BIT_CFG_2",
	"COLOR_16BIT_CFG_3",
	"COLOR_18BIT_CFG_1",
	"COLOR_18BIT_CFG_2(loosely)",
	"COLOR_24BIT",
	"COLOR_20BIT_LOOSE",
	"COLOR_24_BIT_YCBCR",
	"COLOR_16BIT_YCBCR",
	"COLOR_30BIT",
	"COLOR_36BIT",
	"COLOR_12BIT",
	"COLOR_RGB_111",
	"COLOR_RGB_332",
	"COLOR_RGB_444",
	"un-support type",
};

static struct DSI_Phy_s dsi_phy_config;
static struct DSI_Vid_s dsi_vconf;
static struct DSI_Config_s *dConf;
static unsigned char dsi_init_on_table_dft[] = {
	0x05, 1, 0x11,
	0xff, 50,
	0x05, 1, 0x29,
	0xff, 20,
	0xff, 0xff,
};
static unsigned short dsi_rx_n;

static inline void print_mipi_cmd_status(int cnt, unsigned status)
{
	if (cnt == 0) {
		DPRINT("cmd error: status=0x%04x, int0=0x%06x, int1=0x%06x\n",
			status,
			dsi_reg_read(MIPI_DSI_DWC_INT_ST0_OS),
			dsi_reg_read(MIPI_DSI_DWC_INT_ST1_OS));
	}
}

int dsi_init_table_detect(struct device_node *m_node,
		struct DSI_Config_s *mConf, int on_off)
{
	int ret = 0;
	unsigned char *dsi_table;
	unsigned char propname[15];
	int i, j;
	int n_max;
	unsigned int *para; /* num 100 array */
	unsigned int val;

	if (mConf == NULL)
		return -1;
	if (on_off) {
		if (mConf->dsi_init_on == NULL)
			return -1;
		dsi_table = mConf->dsi_init_on;
		n_max = DSI_INIT_ON_MAX;
		sprintf(propname, "dsi_init_on");
	} else {
		if (mConf->dsi_init_off == NULL)
			return -1;
		dsi_table = mConf->dsi_init_off;
		n_max = DSI_INIT_OFF_MAX;
		sprintf(propname, "dsi_init_off");
	}

	para = kmalloc(sizeof(unsigned int)*100, GFP_KERNEL);
	if (para == NULL) {
		DPRINT("[_get_lcd_model_timing]: Not enough memory\n");
		return -1;
	}
	ret = of_property_read_u32_index(m_node, propname, 0, &para[0]);
	if (ret) {
		DPRINT("faild to get %s\n", propname);
		kfree(para);
		return ret;
	}
	i = 0;
	while (i < n_max) {
		ret = of_property_read_u32_index(m_node, propname, i, &val);
		if (val == 0xff) {
			ret = of_property_read_u32_index(m_node,
				propname, (i+1), &val);
			i += 2;
			if (val == 0xff)
				break;
		} else if ((val & 0xf) == 0x0) {
			DPRINT("get %s wrong ", propname);
			DPRINT("data_type: 0x%02x\n", val);
			break;
		} else {
			ret = of_property_read_u32_index(m_node,
				propname, (i+DSI_CMD_INDEX), &val);
			if (val > n_max)
				break;
			else
				i = i+(DSI_CMD_INDEX+1)+(val&0xff);
		}
	}
	i = (i > n_max) ? n_max : i;
	ret = of_property_read_u32_array(m_node, propname, &para[0], i);
	if (ret) {
		DPRINT("faild to get %s\n", propname);
	} else {
		lcd_print("%s: ", propname);
		for (j = 0; j < i; j++) {
			dsi_table[j] = (unsigned char)(para[j] & 0xff);
			lcd_print("0x%02x ", dsi_table[j]);
		}
		lcd_print("\n");
	}

	kfree(para);
	return ret;
}

static void dsi_init_table_print(struct DSI_Config_s *mConf, int on_off)
{
	int i, j, n;
	int n_max;
	unsigned char *dsi_table;

	if (on_off) {
		if (mConf->dsi_init_on == NULL)
			return;
		dsi_table = mConf->dsi_init_on;
		n_max = DSI_INIT_ON_MAX;
		DPRINT("DSI INIT ON:\n");
	} else {
		if (mConf->dsi_init_off == NULL)
			return;
		dsi_table = mConf->dsi_init_off;
		n_max = DSI_INIT_OFF_MAX;
		DPRINT("DSI INIT OFF:\n");
	}
	i = 0;
	while (i < n_max) {
		if (dsi_table[i] == 0xff) {
			n = 2;
			if (dsi_table[i+1] == 0xff) {
				DPRINT("    0x%02x,0x%02x,\n", dsi_table[i],
					dsi_table[i+1]);
				break;
			} else {
				DPRINT("    0x%02x,%d,\n", dsi_table[i],
					dsi_table[i+1]);
			}
		} else if ((dsi_table[i] & 0xf) == 0x0) {
			DPRINT("dsi_init_%s wrong data_type: 0x%02x\n",
				on_off ? "on" : "off", dsi_table[i]);
			break;
		} else {
			n = (DSI_CMD_INDEX + 1) + dsi_table[i+DSI_CMD_INDEX];
			DPRINT("    ");
			for (j = 0; j < n; j++) {
				if (j == DSI_CMD_INDEX)
					DPRINT("%d,", dsi_table[i+j]);
				else
					DPRINT("0x%02x,", dsi_table[i+j]);
			}
			DPRINT("\n");
		}
		i += n;
	}
}

static void print_info(void)
{
	unsigned temp;

	if (dConf == NULL) {
		DPRINT("dsi config is NULL\n");
		return;
	}
	DPRINT("================================================\n");
	DPRINT("MIPI DSI Config\n");
	DPRINT(" Lane Num:              %d\n", dConf->lane_num);
	DPRINT(" Bit Rate max:          %dMHz\n", dConf->bit_rate_max);
	DPRINT(" Bit Rate:              %d.%03dMHz\n",
		(dConf->bit_rate / 1000000),
		(dConf->bit_rate % 1000000) / 1000);
	DPRINT(" Pclk lanebyte factor:  %d\n",
		((dConf->factor_numerator * 100 /
		dConf->factor_denominator) + 5) / 10);
	DPRINT(" Operation mode:\n");
	DPRINT("     init:              %s\n",
		((dConf->operation_mode>>BIT_OP_MODE_INIT) & 1) ?
		"COMMAND":"VIDEO");
	DPRINT("     display:           %s\n",
		((dConf->operation_mode>>BIT_OP_MODE_DISP) & 1) ?
		"COMMAND":"VIDEO");
	DPRINT(" Transfer control:\n");
	DPRINT("     clk auto stop:     %d\n",
		((dConf->transfer_ctrl>>BIT_TRANS_CTRL_CLK) & 1));
	DPRINT("     transfer switch:   %d\n",
		((dConf->transfer_ctrl>>BIT_TRANS_CTRL_SWITCH) & 3));
	DPRINT(" Video mode type:       %s\n",
		video_mode_type[dConf->video_mode_type]);

	/* DPRINT(" Venc format:           %d\n", dConf->venc_fmt); */
	DPRINT(" Data Format:           %s\n\n",
		video_data_type_table[dConf->dpi_data_format]);
	/* DPRINT(" POLARITY:              HIGH ACTIVE\n"); */
	/* DPRINT(" Enable CRC/ECC/BTA\n"); */

	temp = dConf->bit_rate / 8 / dsi_phy_config.lp_tesc;
	DPRINT("DSI LP escape clock:    %d.%03dMHz\n",
		(temp / 1000000), (temp % 1000000) / 1000);

	dsi_init_table_print(dConf, 1); /* dsi_init_on table */
	dsi_init_table_print(dConf, 0); /* dsi_init_off table */

	DPRINT("DSI INIT EXTERN:        %d\n", dConf->lcd_extern_init);
	DPRINT("================================================\n");
}

static void print_dphy_info(void)
{
	unsigned temp;

	temp = ((1000000 * 100) / (dConf->bit_rate / 1000)) * 8;
	DPRINT("================================================\n");
	DPRINT("MIPI DSI DPHY timing (unit: ns)\n"
	" UI:                  %d.%02d\n"
	" LP LPX:              %d\n"
	" LP TA_SURE:          %d\n"
	" LP TA_GO:            %d\n"
	" LP TA_GET:           %d\n"
	" HS EXIT:             %d\n"
	" HS TRAIL:            %d\n"
	" HS ZERO:             %d\n"
	" HS PREPARE:          %d\n"
	" CLK TRAIL:           %d\n"
	" CLK POST:            %d\n"
	" CLK ZERO:            %d\n"
	" CLK PREPARE:         %d\n"
	" CLK PRE:             %d\n"
	" INIT:                %d\n"
	" WAKEUP:              %d\n",
	(temp / 8 / 100), ((temp / 8) % 100),
	(temp * dsi_phy_config.lp_lpx / 100),
	(temp * dsi_phy_config.lp_ta_sure / 100),
	(temp * dsi_phy_config.lp_ta_go / 100),
	(temp * dsi_phy_config.lp_ta_get / 100),
	(temp * dsi_phy_config.hs_exit / 100),
	(temp * dsi_phy_config.hs_trail / 100),
	(temp * dsi_phy_config.hs_zero / 100),
	(temp * dsi_phy_config.hs_prepare / 100),
	(temp * dsi_phy_config.clk_trail / 100),
	(temp * dsi_phy_config.clk_post / 100),
	(temp * dsi_phy_config.clk_zero / 100),
	(temp * dsi_phy_config.clk_prepare / 100),
	(temp * dsi_phy_config.clk_pre / 100),
	(temp * dsi_phy_config.init / 100),
	(temp * dsi_phy_config.wakeup / 100));
	DPRINT("================================================\n");
}

/* *************************************************************
 * Function: check_phy_st
 * Check the status of the dphy: phylock and stopstateclklane,
   to decide if the DPHY is ready
 * ************************************************************* */
static void check_phy_status(void)
{
	while (dsi_reg_getb(MIPI_DSI_DWC_PHY_STATUS_OS, BIT_PHY_LOCK, 1) == 0)
		udelay(6);
	while (dsi_reg_getb(MIPI_DSI_DWC_PHY_STATUS_OS,
		BIT_PHY_STOPSTATECLKLANE, 1) == 0) {
		lcd_print(" Waiting STOP STATE LANE\n");
		udelay(6);
	}
}

/* *************************************************************
 * Function: set_mipi_dcs
 * Configure relative registers in command mode
 * Parameters:   int trans_type, // 0: high speed, 1: low power
 *               int req_ack,    // 1: request ack, 0: do not need ack
 *               int tear_en     // 1: enable tear ack, 0: disable tear ack
 * ************************************************************* */
static void set_mipi_dcs(int trans_type, int req_ack, int tear_en)
{
	dsi_reg_write(MIPI_DSI_DWC_CMD_MODE_CFG_OS,
		(trans_type << BIT_MAX_RD_PKT_SIZE) |
		(trans_type << BIT_DCS_LW_TX)    |
		(trans_type << BIT_DCS_SR_0P_TX) |
		(trans_type << BIT_DCS_SW_1P_TX) |
		(trans_type << BIT_DCS_SW_0P_TX) |
		(trans_type << BIT_GEN_LW_TX)    |
		(trans_type << BIT_GEN_SR_2P_TX) |
		(trans_type << BIT_GEN_SR_1P_TX) |
		(trans_type << BIT_GEN_SR_0P_TX) |
		(trans_type << BIT_GEN_SW_2P_TX) |
		(trans_type << BIT_GEN_SW_1P_TX) |
		(trans_type << BIT_GEN_SW_0P_TX) |
		(req_ack << BIT_ACK_RQST_EN)     |
		(tear_en << BIT_TEAR_FX_EN));

	if (tear_en == MIPI_DCS_ENABLE_TEAR) {
		/* Enable Tear Interrupt if tear_en is valid */
		lcd_reg_set_mask(MIPI_DSI_TOP_INTR_CNTL_STAT,
			(0x1 << BIT_EDPITE_INT_EN));
		/* Enable Measure Vsync */
		lcd_reg_set_mask(MIPI_DSI_TOP_MEAS_CNTL,
			(0x1 << BIT_VSYNC_MEAS_EN) | (0x1 << BIT_TE_MEAS_EN));
	}

	/* Packet header settings */
	dsi_reg_write(MIPI_DSI_DWC_PCKHDL_CFG_OS,
		(1 << BIT_CRC_RX_EN)  |
		(1 << BIT_ECC_RX_EN)  |
		(0 << BIT_BTA_EN)     |
		(0 << BIT_EOTP_RX_EN) |
		(0 << BIT_EOTP_TX_EN));
}

#if 0
/* *************************************************************
 * Function: set_mipi_int
 * Configure relative registers for mipi interrupt
 * ************************************************************* */
static void set_mipi_int(void)
{
	dsi_reg_write(MIPI_DSI_DWC_INT_MSK0_OS, 0);
	dsi_reg_write(MIPI_DSI_DWC_INT_MSK1_OS, 0);
}
#endif

#ifdef DSI_CMD_READ_VALID
static void dsi_bta_control(int en)
{
	if (en) {
		dsi_reg_setb(MIPI_DSI_DWC_CMD_MODE_CFG_OS,
			MIPI_DSI_DCS_REQ_ACK, BIT_ACK_RQST_EN, 1);
		dsi_reg_setb(MIPI_DSI_DWC_PCKHDL_CFG_OS,
			MIPI_DSI_DCS_REQ_ACK, BIT_BTA_EN, 1);
	} else {
		dsi_reg_setb(MIPI_DSI_DWC_PCKHDL_CFG_OS,
			MIPI_DSI_DCS_NO_ACK, BIT_BTA_EN, 1);
		dsi_reg_setb(MIPI_DSI_DWC_CMD_MODE_CFG_OS,
			MIPI_DSI_DCS_NO_ACK, BIT_ACK_RQST_EN, 1);
	}
}

/* *************************************************************
 * Function: generic_if_rd
 * Generic interface read, address has to be MIPI_DSI_DWC_GEN_PLD_DATA_OS
 * ************************************************************* */
static unsigned int generic_if_rd(unsigned int address)
{
	unsigned int data_out;

	if (address != MIPI_DSI_DWC_GEN_PLD_DATA_OS)
		DPRINT(" Error Address : %x\n", address);

	data_out = dsi_reg_read(address);
	return data_out;
}
#endif

/* *************************************************************
 * Function: generic_if_wr
 * Generic interface write, address has to be
			MIPI_DSI_DWC_GEN_PLD_DATA_OS,
			MIPI_DSI_DWC_GEN_HDR_OS,
			MIPI_DSI_DWC_GEN_VCID_OS
 * ************************************************************* */
static unsigned int generic_if_wr(unsigned int address, unsigned int data_in)
{
	if ((address != MIPI_DSI_DWC_GEN_HDR_OS) &&
		(address != MIPI_DSI_DWC_GEN_PLD_DATA_OS)) {
		DPRINT(" Error Address : 0x%x\n", address);
	}

	lcd_print("address 0x%x = 0x%08x\n", address, data_in);
	dsi_reg_write(address, data_in);

	return 0;
}

/* *************************************************************
 * Function: wait_bta_ack
 * Poll to check if the BTA ack is finished
 * ************************************************************* */
static void wait_bta_ack(void)
{
	unsigned int phy_status, i;

	/* Check if phydirection is RX */
	i = CMD_TIMEOUT_CNT;
	do {
		udelay(10);
		i--;
		phy_status = dsi_reg_read(MIPI_DSI_DWC_PHY_STATUS_OS);
	} while ((((phy_status & 0x2) >> BIT_PHY_DIRECTION) == 0x0) && (i > 0));
	if (i == 0)
		DPRINT("phy direction error: RX\n");

	/* Check if phydirection is return to TX */
	i = CMD_TIMEOUT_CNT;
	do {
		udelay(10);
		i--;
		phy_status = dsi_reg_read(MIPI_DSI_DWC_PHY_STATUS_OS);
	} while (((phy_status & 0x2) >> BIT_PHY_DIRECTION) == 0x1);
	if (i == 0)
		DPRINT("phy direction error: TX\n");
}

/* *************************************************************
 * Function: wait_cmd_fifo_empty
 * Poll to check if the generic command fifo is empty
 * ************************************************************* */
static void wait_cmd_fifo_empty(void)
{
	unsigned int cmd_status;
	int i = CMD_TIMEOUT_CNT;

	do {
		udelay(10);
		i--;
		cmd_status = dsi_reg_read(MIPI_DSI_DWC_CMD_PKT_STATUS_OS);
	} while ((((cmd_status >> BIT_GEN_CMD_EMPTY) & 0x1) != 0x1) && (i > 0));
	print_mipi_cmd_status(i, cmd_status);
}

#if 0
/* *************************************************************
 * Function: wait_for_generic_read_response
 * Wait for generic read response
 * ************************************************************* */
static unsigned int wait_for_generic_read_response(void)
{
	unsigned int timeout, phy_status, data_out;

	phy_status = dsi_reg_read(MIPI_DSI_DWC_PHY_STATUS_OS);
	for (timeout = 0; timeout < 50; timeout++) {
		if (((phy_status & 0x40) >> BIT_PHY_RXULPSESC0LANE) == 0x0)
			break;
		phy_status = dsi_reg_read(MIPI_DSI_DWC_PHY_STATUS_OS);
		udelay(1);
	}
	phy_status = dsi_reg_read(MIPI_DSI_DWC_PHY_STATUS_OS);
	for (timeout = 0; timeout < 50; timeout++) {
		if (((phy_status & 0x40) >> BIT_PHY_RXULPSESC0LANE) == 0x1)
			break;
		phy_status = dsi_reg_read(MIPI_DSI_DWC_PHY_STATUS_OS);
		udelay(1);
	}

	data_out = dsi_reg_read(MIPI_DSI_DWC_GEN_PLD_DATA_OS);
	return data_out;
}

/* *************************************************************
 * Function: generic_read_packet_0_para
 * Generic Read Packet 0 Parameter with Generic Interface
 * Supported DCS Command: DCS_SET_ADDRESS_MODE,
			DCS_SET_GAMMA_CURVE,
			DCS_SET_PIXEL_FORMAT,
			DCS_SET_TEAR_ON
 * ************************************************************* */
static unsigned int generic_read_packet_0_para(unsigned char data_type,
		unsigned char vc_id, unsigned char dcs_command)
{
	unsigned int read_data;

	/* lcd_print(" para is %x, dcs_command is %x\n", para, dcs_command); */
	/* lcd_print(" vc_id %x, data_type is %x\n", vc_id, data_type); */
	generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS,
		((0 << BIT_GEN_WC_MSBYTE)                           |
		(((unsigned int)dcs_command) << BIT_GEN_WC_LSBYTE)  |
		(((unsigned int)vc_id) << BIT_GEN_VC)               |
		(((unsigned int)data_type) << BIT_GEN_DT)));

	read_data = wait_for_generic_read_response();

	return read_data;
}
#endif

static void dsi_set_max_return_pkt_size(struct DSI_Cmd_Request_s *req)
{
	unsigned int d_para[2];

	d_para[0] = (unsigned int)(req->payload[2] & 0xff);
	d_para[1] = (unsigned int)(req->payload[3] & 0xff);
	dsi_rx_n = (unsigned short)((d_para[1] << 8) | d_para[0]);
	generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS,
		((d_para[1] << BIT_GEN_WC_MSBYTE)          |
		(d_para[0] << BIT_GEN_WC_LSBYTE)           |
		(((unsigned int)req->vc_id) << BIT_GEN_VC) |
		(DT_SET_MAX_RET_PKT_SIZE << BIT_GEN_DT)));
	if (req->req_ack == MIPI_DSI_DCS_REQ_ACK)
		wait_bta_ack();
	else if (req->req_ack == MIPI_DSI_DCS_NO_ACK)
		wait_cmd_fifo_empty();
}

#ifdef DSI_CMD_READ_VALID
static int dsi_generic_read_packet(struct DSI_Cmd_Request_s *req,
		unsigned char *r_data)
{
	unsigned int d_para[2], read_data;
	unsigned int i, j, done;

	switch (req->data_type) {
	case DT_GEN_RD_1:
		d_para[0] = (req->pld_count == 0) ?
			0 : (((unsigned int)req->payload[2]) & 0xff);
		d_para[1] = 0;
		break;
	case DT_GEN_RD_2:
		d_para[0] = (req->pld_count == 0) ?
			0 : (((unsigned int)req->payload[2]) & 0xff);
		d_para[1] = (req->pld_count < 2) ?
			0 : (((unsigned int)req->payload[3]) & 0xff);
		break;
	case DT_GEN_RD_0:
	default:
		d_para[0] = 0;
		d_para[1] = 0;
		break;
	}

	if (MIPI_DSI_DCS_ACK_TYPE == MIPI_DSI_DCS_NO_ACK)
		dsi_bta_control(1);
	generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS,
		((d_para[1] << BIT_GEN_WC_MSBYTE)          |
		(d_para[0] << BIT_GEN_WC_LSBYTE)           |
		(((unsigned int)req->vc_id) << BIT_GEN_VC) |
		(((unsigned int)req->data_type) << BIT_GEN_DT)));
	wait_bta_ack();
	i = 0;
	done = 0;
	while (done == 0) {
		read_data = generic_if_rd(MIPI_DSI_DWC_GEN_PLD_DATA_OS);
		for (j = 0; j < 4; j++) {
			if (i < dsi_rx_n) {
				r_data[i] = (unsigned char)
					((read_data >> (j*8)) & 0xff);
				i++;
			} else {
				done = 1;
				break;
			}
		}
	}
	if (MIPI_DSI_DCS_ACK_TYPE == MIPI_DSI_DCS_NO_ACK)
		dsi_bta_control(0);

	return dsi_rx_n;
}

static int dsi_dcs_read_packet(struct DSI_Cmd_Request_s *req,
		unsigned char *r_data)
{
	unsigned int d_command, read_data;
	unsigned int i, j, done;

	d_command = ((unsigned int)req->payload[2]) & 0xff;

	if (MIPI_DSI_DCS_ACK_TYPE == MIPI_DSI_DCS_NO_ACK)
		dsi_bta_control(1);
	generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS,
		((0 << BIT_GEN_WC_MSBYTE)                  |
		(d_command << BIT_GEN_WC_LSBYTE)           |
		(((unsigned int)req->vc_id) << BIT_GEN_VC) |
		(((unsigned int)req->data_type) << BIT_GEN_DT)));
	wait_bta_ack();
	i = 0;
	done = 0;
	while (done == 0) {
		read_data = generic_if_rd(MIPI_DSI_DWC_GEN_PLD_DATA_OS);
		for (j = 0; j < 4; j++) {
			if (i < dsi_rx_n) {
				r_data[i] = (unsigned char)
					((read_data >> (j*8)) & 0xff);
				i++;
			} else {
				done = 1;
				break;
			}
		}
	}

	if (MIPI_DSI_DCS_ACK_TYPE == MIPI_DSI_DCS_NO_ACK)
		dsi_bta_control(0);

	return dsi_rx_n;
}
#endif

/* *************************************************************
 * Function: generic_write_short_packet
 * Generic Write Short Packet with Generic Interface
 * Supported Data Type: DT_GEN_SHORT_WR_0,
			DT_GEN_SHORT_WR_1,
			DT_GEN_SHORT_WR_2,
 * ************************************************************* */
static void dsi_generic_write_short_packet(struct DSI_Cmd_Request_s *req)
{
	unsigned int d_para[2];

	switch (req->data_type) {
	case DT_GEN_SHORT_WR_1:
		d_para[0] = (req->pld_count == 0) ?
			0 : (((unsigned int)req->payload[2]) & 0xff);
		d_para[1] = 0;
		break;
	case DT_GEN_SHORT_WR_2:
		d_para[0] = (req->pld_count == 0) ?
			0 : (((unsigned int)req->payload[2]) & 0xff);
		d_para[1] = (req->pld_count < 2) ?
			0 : (((unsigned int)req->payload[3]) & 0xff);
		break;
	case DT_GEN_SHORT_WR_0:
	default:
		d_para[0] = 0;
		d_para[1] = 0;
		break;
	}

	generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS,
		((d_para[1] << BIT_GEN_WC_MSBYTE)          |
		(d_para[0] << BIT_GEN_WC_LSBYTE)           |
		(((unsigned int)req->vc_id) << BIT_GEN_VC) |
		(((unsigned int)req->data_type) << BIT_GEN_DT)));
	if (req->req_ack == MIPI_DSI_DCS_REQ_ACK)
		wait_bta_ack();
	else if (req->req_ack == MIPI_DSI_DCS_NO_ACK)
		wait_cmd_fifo_empty();
}

/* *************************************************************
 * Function: dcs_write_short_packet
 * DCS Write Short Packet with Generic Interface
 * Supported Data Type: DT_DCS_SHORT_WR_0, DT_DCS_SHORT_WR_1,
 * ************************************************************* */
static void dsi_dcs_write_short_packet(struct DSI_Cmd_Request_s *req)
{
	unsigned int d_command, d_para;

	d_command = ((unsigned int)req->payload[2]) & 0xff;
	d_para = (req->pld_count < 2) ?
		0 : (((unsigned int)req->payload[3]) & 0xff);

	generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS,
		((d_para << BIT_GEN_WC_MSBYTE)             |
		(d_command << BIT_GEN_WC_LSBYTE)           |
		(((unsigned int)req->vc_id) << BIT_GEN_VC) |
		(((unsigned int)req->data_type) << BIT_GEN_DT)));
	if (req->req_ack == MIPI_DSI_DCS_REQ_ACK)
		wait_bta_ack();
	else if (req->req_ack == MIPI_DSI_DCS_NO_ACK)
		wait_cmd_fifo_empty();
}

/* *************************************************************
 * Function: dsi_write_long_packet
 * Write Long Packet with Generic Interface
 * Supported Data Type: DT_GEN_LONG_WR, DT_DCS_LONG_WR
 * ************************************************************* */
static void dsi_write_long_packet(struct DSI_Cmd_Request_s *req)
{
	unsigned int d_command, payload_data, header_data;
	unsigned int cmd_status;
	unsigned int i, j, data_index, n, temp;

	/* payload[2] start (payload[0]: data_type, payload[1]: data_cnt) */
	data_index = DSI_CMD_INDEX + 1;
	d_command = ((unsigned int)req->payload[data_index]) & 0xff;

	/* Write Payload Register First */
	n = (req->pld_count+3)/4;
	for (i = 0; i < n; i++) {
		payload_data = 0;
		if (i < (req->pld_count/4))
			temp = 4;
		else
			temp = req->pld_count % 4;
		for (j = 0; j < temp; j++) {
			payload_data |= (((unsigned int)
				req->payload[data_index+(i*4)+j]) << (j*8));
		}

		/* Check the pld fifo status before write to it,
		do not need check every word */
		if ((i == (n/3)) || (i == (n/2))) {
			j = CMD_TIMEOUT_CNT;
			do {
				udelay(10);
				j--;
				cmd_status = dsi_reg_read(
						MIPI_DSI_DWC_CMD_PKT_STATUS_OS);
			} while ((((cmd_status >> BIT_GEN_PLD_W_FULL) & 0x1) ==
				0x1) && (j > 0));
			print_mipi_cmd_status(j, cmd_status);
		}
		/* Use direct memory write to save time when in
		WRITE_MEMORY_CONTINUE */
		if (d_command == DCS_WRITE_MEMORY_CONTINUE) {
			dsi_reg_write(MIPI_DSI_DWC_GEN_PLD_DATA_OS,
				payload_data);
		} else {
			generic_if_wr(MIPI_DSI_DWC_GEN_PLD_DATA_OS,
				payload_data);
		}
	}

	/* Check cmd fifo status before write to it */
	j = CMD_TIMEOUT_CNT;
	do {
		udelay(10);
		j--;
		cmd_status = dsi_reg_read(MIPI_DSI_DWC_CMD_PKT_STATUS_OS);
	} while ((((cmd_status >> BIT_GEN_CMD_FULL) & 0x1) == 0x1) && (j > 0));
	print_mipi_cmd_status(j, cmd_status);
	/* Write Header Register */
	/* include command */
	header_data = ((((unsigned int)req->pld_count) << BIT_GEN_WC_LSBYTE) |
			(((unsigned int)req->vc_id) << BIT_GEN_VC)           |
			(((unsigned int)req->data_type) << BIT_GEN_DT));
	generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS, header_data);
	if (req->req_ack == MIPI_DSI_DCS_REQ_ACK)
		wait_bta_ack();
	else if (req->req_ack == MIPI_DSI_DCS_NO_ACK)
		wait_cmd_fifo_empty();
}

/* *************************************************************
 * Function: dsi_write_cmd
 * Supported Data Type: DT_GEN_SHORT_WR_0, DT_GEN_SHORT_WR_1, DT_GEN_SHORT_WR_2,
			DT_DCS_SHORT_WR_0, DT_DCS_SHORT_WR_1,
			DT_GEN_LONG_WR, DT_DCS_LONG_WR,
			DT_SET_MAX_RET_PKT_SIZE
			DT_GEN_RD_0, DT_GEN_RD_1, DT_GEN_RD_2,
			DT_DCS_RD_0
 * Return:              command number
 * ************************************************************* */
int dsi_write_cmd(unsigned char *payload)
{
	int i = 0, j = 0, num = 0;
#ifdef DSI_CMD_READ_VALID
	int k = 0, n = 0;
	unsigned char rd_data[100];
#endif
	struct DSI_Cmd_Request_s dsi_cmd_req;
	unsigned char vc_id = MIPI_DSI_VIRTUAL_CHAN_ID;
	unsigned int req_ack = MIPI_DSI_DCS_ACK_TYPE;

	/* mipi command(payload) */
	/* format:  data_type, num, data.... */
	/* special: data_type=0xff,
		    num<0xff means delay ms, num=0xff means ending. */
	while (i < DSI_CMD_SIZE_MAX) {
		if (payload[i] == 0xff) {
			j = 2;
			if (payload[i+1] == 0xff)
				break;
			else
				mdelay(payload[i+1]);
		} else if ((payload[i] & 0xf) == 0x0) {
			DPRINT("[error]dsi data_type: 0x%02x\n", payload[i]);
			break;
		} else {
			/* payload[i+DSI_CMD_INDEX] is data count */
			j = (DSI_CMD_INDEX + 1) + payload[i+DSI_CMD_INDEX];
			dsi_cmd_req.data_type = payload[i];
			dsi_cmd_req.vc_id = (vc_id & 0x3);
			dsi_cmd_req.payload = &payload[i];
			dsi_cmd_req.pld_count = payload[i+DSI_CMD_INDEX];
			dsi_cmd_req.req_ack = req_ack;
			switch (dsi_cmd_req.data_type) {/* analysis data_type */
			case DT_GEN_SHORT_WR_0:
			case DT_GEN_SHORT_WR_1:
			case DT_GEN_SHORT_WR_2:
				dsi_generic_write_short_packet(&dsi_cmd_req);
				break;
			case DT_DCS_SHORT_WR_0:
			case DT_DCS_SHORT_WR_1:
				dsi_dcs_write_short_packet(&dsi_cmd_req);
				break;
			case DT_DCS_LONG_WR:
			case DT_GEN_LONG_WR:
				dsi_write_long_packet(&dsi_cmd_req);
				break;
			case DT_TURN_ON:
				dsi_reg_setb(MIPI_DSI_TOP_CNTL, 1, 2, 1);
				mdelay(20); /* wait for vsync trigger */
				dsi_reg_setb(MIPI_DSI_TOP_CNTL, 0, 2, 1);
				mdelay(20); /* wait for vsync trigger */
				break;
			case DT_SHUT_DOWN:
				dsi_reg_setb(MIPI_DSI_TOP_CNTL, 1, 2, 1);
				mdelay(20); /* wait for vsync trigger */
				break;
			case DT_SET_MAX_RET_PKT_SIZE:
				dsi_set_max_return_pkt_size(&dsi_cmd_req);
				break;
#ifdef DSI_CMD_READ_VALID
			case DT_GEN_RD_0:
			case DT_GEN_RD_1:
			case DT_GEN_RD_2:
				/* need BTA ack */
				dsi_cmd_req.req_ack = MIPI_DSI_DCS_REQ_ACK;
				dsi_cmd_req.pld_count =
					(dsi_cmd_req.pld_count > 2) ?
					2 : dsi_cmd_req.pld_count;
				n = dsi_generic_read_packet(&dsi_cmd_req,
						&rd_data[0]);
				DPRINT("generic read data");
				for (k = 0; k < dsi_cmd_req.pld_count; k++) {
					DPRINT(" 0x%02x",
						dsi_cmd_req.payload[k+2]);
				}
				DPRINT(":\n");
				for (k = 0; k < n; k++)
					DPRINT("0x%02x ", rd_data[k]);
				DPRINT("\n");
				break;
			case DT_DCS_RD_0:
				/* need BTA ack */
				dsi_cmd_req.req_ack = MIPI_DSI_DCS_REQ_ACK;
				n = dsi_dcs_read_packet(&dsi_cmd_req,
					&rd_data[0]);
				DPRINT("dcs read data 0x%02x:\n",
					dsi_cmd_req.payload[2]);
				for (k = 0; k < n; k++)
					DPRINT("0x%02x ", rd_data[k]);
				DPRINT("\n");
				break;
#endif
			default:
				DPRINT("[warning]dsi un-support data_type: ");
				DPRINT("0x%02x\n", dsi_cmd_req.data_type);
				break;
			}
		}
		i += j;
		num++;
	}

	return num;
}

#ifdef DSI_CMD_READ_VALID
/* *************************************************************
 * Function: dsi_read_single
 * Supported Data Type: DT_GEN_RD_0, DT_GEN_RD_1, DT_GEN_RD_2,
			DT_DCS_RD_0
 * Return:              data count
 * ************************************************************* */
int dsi_read_single(unsigned char *payload, unsigned char *rd_data,
		unsigned int rd_byte_len)
{
	int num = 0;
	unsigned char temp[4];
	unsigned char vc_id = MIPI_DSI_VIRTUAL_CHAN_ID;
	unsigned int req_ack;
	struct DSI_Cmd_Request_s dsi_cmd_req;

	req_ack = MIPI_DSI_DCS_ACK_TYPE;
	dsi_cmd_req.data_type = DT_SET_MAX_RET_PKT_SIZE;
	dsi_cmd_req.vc_id = (vc_id & 0x3);
	temp[0] = dsi_cmd_req.data_type;
	temp[1] = 2;
	temp[2] = (unsigned char)((rd_byte_len >> 0) & 0xff);
	temp[3] = (unsigned char)((rd_byte_len >> 8) & 0xff);
	dsi_cmd_req.payload = &temp[0];
	dsi_cmd_req.pld_count = 2;
	dsi_cmd_req.req_ack = req_ack;
	dsi_set_max_return_pkt_size(&dsi_cmd_req);

	/* payload struct: */
	/* data_type, data_cnt, command, parameters... */
	req_ack = MIPI_DSI_DCS_REQ_ACK; /* need BTA ack */
	dsi_cmd_req.data_type = payload[0];
	dsi_cmd_req.vc_id = (vc_id & 0x3);
	dsi_cmd_req.payload = &payload[0];
	dsi_cmd_req.pld_count = payload[DSI_CMD_INDEX];
	dsi_cmd_req.req_ack = req_ack;
	switch (dsi_cmd_req.data_type) {/* analysis data_type */
	case DT_GEN_RD_0:
	case DT_GEN_RD_1:
	case DT_GEN_RD_2:
		num = dsi_generic_read_packet(&dsi_cmd_req, rd_data);
		break;
	case DT_DCS_RD_0:
		num = dsi_dcs_read_packet(&dsi_cmd_req, rd_data);
		break;
	default:
		DPRINT("dsi read un-support data_type: 0x%02x\n",
			dsi_cmd_req.data_type);
		break;
	}

	return num;
}
#else
int dsi_read_single(unsigned char *payload, unsigned char *rd_data,
		unsigned int rd_byte_len)
{
	DPRINT("Don't support mipi-dsi read command\n");
	return 0;
}
#endif

static void set_dsi_phy_config(struct DSI_Phy_s *dphy, unsigned int dsi_ui)
{
	unsigned int temp, t_ui;

	t_ui = (1000000 * 100) / (dsi_ui / 1000); /* 0.01ns*100 */
	temp = t_ui * 8; /* lane_byte cycle time */

	dphy->lp_tesc = ((DPHY_TIME_LP_TESC(t_ui) + temp - 1) / temp) & 0xff;
	dphy->lp_lpx = ((DPHY_TIME_LP_LPX(t_ui) + temp - 1) / temp) & 0xff;
	dphy->lp_ta_sure = ((DPHY_TIME_LP_TA_SURE(t_ui) + temp - 1) / temp) &
			0xff;
	dphy->lp_ta_go = ((DPHY_TIME_LP_TA_GO(t_ui) + temp - 1) / temp) & 0xff;
	dphy->lp_ta_get = ((DPHY_TIME_LP_TA_GETX(t_ui) + temp - 1) / temp) &
			0xff;
	dphy->hs_exit = ((DPHY_TIME_HS_EXIT(t_ui) + temp - 1) / temp) & 0xff;
	dphy->hs_trail = ((DPHY_TIME_HS_TRAIL(t_ui) + temp - 1) / temp) & 0xff;
	dphy->hs_prepare = ((DPHY_TIME_HS_PREPARE(t_ui) + temp - 1) / temp) &
			0xff;
	dphy->hs_zero = ((DPHY_TIME_HS_ZERO(t_ui) + temp - 1) / temp) & 0xff;
	dphy->clk_trail = ((DPHY_TIME_CLK_TRAIL(t_ui) + temp - 1) / temp) &
			0xff;
	dphy->clk_post = ((DPHY_TIME_CLK_POST(t_ui) + temp - 1) / temp) & 0xff;
	dphy->clk_prepare = ((DPHY_TIME_CLK_PREPARE(t_ui) + temp - 1) / temp) &
			0xff;
	dphy->clk_zero = ((DPHY_TIME_CLK_ZERO(t_ui) + temp - 1) / temp) & 0xff;
	dphy->clk_pre = ((DPHY_TIME_CLK_PRE(t_ui) + temp - 1) / temp) & 0xff;
	dphy->init = (DPHY_TIME_INIT(t_ui) + temp - 1) / temp;
	dphy->wakeup = (DPHY_TIME_WAKEUP(t_ui) + temp - 1) / temp;

	lcd_print("lp_tesc = 0x%02x\n"
		"lp_lpx = 0x%02x\n"
		"lp_ta_sure = 0x%02x\n"
		"lp_ta_go = 0x%02x\n"
		"lp_ta_get = 0x%02x\n"
		"hs_exit = 0x%02x\n"
		"hs_trail = 0x%02x\n"
		"hs_zero = 0x%02x\n"
		"hs_prepare = 0x%02x\n"
		"clk_trail = 0x%02x\n"
		"clk_post = 0x%02x\n"
		"clk_zero = 0x%02x\n"
		"clk_prepare = 0x%02x\n"
		"clk_pre = 0x%02x\n"
		"init = 0x%02x\n"
		"wakeup = 0x%02x\n",
		dphy->lp_tesc, dphy->lp_lpx, dphy->lp_ta_sure, dphy->lp_ta_go,
		dphy->lp_ta_get, dphy->hs_exit, dphy->hs_trail, dphy->hs_zero,
		dphy->hs_prepare, dphy->clk_trail, dphy->clk_post,
		dphy->clk_zero, dphy->clk_prepare, dphy->clk_pre,
		dphy->init, dphy->wakeup);
}

static void dsi_phy_init(struct DSI_Phy_s *dphy, unsigned char lane_num)
{
	/* enable phy clock. */
	dsi_phy_write(MIPI_DSI_PHY_CTRL,  0x1); /* enable DSI top clock. */
	dsi_phy_write(MIPI_DSI_PHY_CTRL,
		(1 << 0)  | /* enable the DSI PLL clock . */
		(1 << 7)  | /* enable pll clock which connected to
				DDR clock path */
		(1 << 8)  | /* enable the clock divider counter */
		(0 << 9)  | /* enable the divider clock out */
		(0 << 10) | /* clock divider. 1: freq/4, 0: freq/2 */
		(0 << 11) | /* 1: select the mipi DDRCLKHS from clock divider,
				0: from PLL clock */
		(0 << 12)); /* enable the byte clock generateion. */
	/* enable the divider clock out */
	dsi_phy_setb(MIPI_DSI_PHY_CTRL,  1, 9, 1);
	/* enable the byte clock generateion. */
	dsi_phy_setb(MIPI_DSI_PHY_CTRL,  1, 12, 1);
	dsi_phy_setb(MIPI_DSI_PHY_CTRL,  1, 31, 1);
	dsi_phy_setb(MIPI_DSI_PHY_CTRL,  0, 31, 1);

	/* 0x05210f08);//0x03211c08 */
	dsi_phy_write(MIPI_DSI_CLK_TIM,
		(dphy->clk_trail | (dphy->clk_post << 8) |
		(dphy->clk_zero << 16) | (dphy->clk_prepare << 24)));
	dsi_phy_write(MIPI_DSI_CLK_TIM1, dphy->clk_pre); /* ?? */
	/* 0x050f090d */
	dsi_phy_write(MIPI_DSI_HS_TIM,
		(dphy->hs_exit | (dphy->hs_trail << 8) |
		(dphy->hs_zero << 16) | (dphy->hs_prepare << 24)));
	/* 0x4a370e0e */
	dsi_phy_write(MIPI_DSI_LP_TIM,
		(dphy->lp_lpx | (dphy->lp_ta_sure << 8) |
		(dphy->lp_ta_go << 16) | (dphy->lp_ta_get << 24)));
	/* ?? //some number to reduce sim time. */
	dsi_phy_write(MIPI_DSI_ANA_UP_TIM, 0x0100);
	/* 0xe20   //30d4 -> d4 to reduce sim time. */
	dsi_phy_write(MIPI_DSI_INIT_TIM, dphy->init);
	/* 0x8d40  //1E848-> 48 to reduct sim time. */
	dsi_phy_write(MIPI_DSI_WAKEUP_TIM, dphy->wakeup);
	/* wait for the LP analog ready. */
	dsi_phy_write(MIPI_DSI_LPOK_TIM,  0x7C);
	/* 1/3 of the tWAKEUP. */
	dsi_phy_write(MIPI_DSI_ULPS_CHECK,  0x927C);
	/* phy TURN watch dog. */
	dsi_phy_write(MIPI_DSI_LP_WCHDOG,  0x1000);
	/* phy ESC command watch dog. */
	dsi_phy_write(MIPI_DSI_TURN_WCHDOG,  0x1000);

	/* Powerup the analog circuit. */
	switch (lane_num) {
	case 1:
		dsi_phy_write(MIPI_DSI_CHAN_CTRL, 0x0e);
		break;
	case 2:
		dsi_phy_write(MIPI_DSI_CHAN_CTRL, 0x0c);
		break;
	case 3:
		dsi_phy_write(MIPI_DSI_CHAN_CTRL, 0x08);
		break;
	case 4:
	default:
		dsi_phy_write(MIPI_DSI_CHAN_CTRL, 0);
		break;
	}
}

static void mipi_dsi_phy_config(struct Lcd_Config_s *pConf)
{
	lcd_print("%s\n", __func__);
	/* Digital */
	/* Power up DSI */
	dsi_reg_write(MIPI_DSI_DWC_PWR_UP_OS, 1);

	/* Setup Parameters of DPHY */
	dsi_reg_write(MIPI_DSI_DWC_PHY_TST_CTRL1_OS, 0x00010044);/* testcode */
	dsi_reg_write(MIPI_DSI_DWC_PHY_TST_CTRL0_OS, 0x2);
	dsi_reg_write(MIPI_DSI_DWC_PHY_TST_CTRL0_OS, 0x0);
	dsi_reg_write(MIPI_DSI_DWC_PHY_TST_CTRL1_OS, 0x00000074);/* testwrite */
	dsi_reg_write(MIPI_DSI_DWC_PHY_TST_CTRL0_OS, 0x2);
	dsi_reg_write(MIPI_DSI_DWC_PHY_TST_CTRL0_OS, 0x0);

	/* Power up D-PHY */
	dsi_reg_write(MIPI_DSI_DWC_PHY_RSTZ_OS, 0xf);

	/* Analog */
	dsi_phy_init(&dsi_phy_config, pConf->lcd_control.mipi_config->lane_num);

	/* Check the phylock/stopstateclklane to decide if the DPHY is ready */
	check_phy_status();

	/* Trigger a sync active for esc_clk */
	dsi_phy_set_mask(MIPI_DSI_PHY_CTRL, (1 << 1));
}

static void dsi_video_config(struct Lcd_Config_s *pConf)
{
	unsigned short h_period, hs_width, hs_bp;
	unsigned int den, num;
	unsigned short v_period, v_active, vs_width, vs_bp;

	h_period = pConf->lcd_basic.h_period;
	hs_width = pConf->lcd_timing.hsync_width;
	hs_bp = pConf->lcd_timing.hsync_bp;
	den = pConf->lcd_control.mipi_config->factor_denominator;
	num = pConf->lcd_control.mipi_config->factor_numerator;

	dsi_vconf.hline = (h_period * den + num - 1) / num;
	dsi_vconf.hsa = (hs_width * den + num - 1) / num;
	dsi_vconf.hbp = (hs_bp * den + num - 1) / num;

	v_period = pConf->lcd_basic.v_period;
	v_active = pConf->lcd_basic.v_active;
	vs_width = pConf->lcd_timing.vsync_width;
	vs_bp = pConf->lcd_timing.vsync_bp;
	dsi_vconf.vsa = vs_width;
	dsi_vconf.vbp = vs_bp;
	dsi_vconf.vfp = v_period - v_active - vs_bp - vs_width;
	dsi_vconf.vact = v_active;

	lcd_print(" ============= VIDEO TIMING SETTING =============\n");
	lcd_print(" HLINE        = %d\n", dsi_vconf.hline);
	lcd_print(" HSA          = %d\n", dsi_vconf.hsa);
	lcd_print(" HBP          = %d\n", dsi_vconf.hbp);
	lcd_print(" VSA          = %d\n", dsi_vconf.vsa);
	lcd_print(" VBP          = %d\n", dsi_vconf.vbp);
	lcd_print(" VFP          = %d\n", dsi_vconf.vfp);
	lcd_print(" VACT         = %d\n", dsi_vconf.vact);
	lcd_print(" ================================================\n");
}

#define DSI_PACKET_HEADER_CRC      6 /* 4(header)+2(CRC) */
static void dsi_non_burst_chunk_config(struct Lcd_Config_s *pConf)
{
	int pixel_per_chunk = 0, num_of_chunk = 0, vid_null_size = 0;
	int byte_per_chunk = 0, total_bytes_per_chunk = 0, chunk_overhead = 0;
	int bit_rate_pclk_factor;
	int lane_num;
	int i, done;

	i = 1;
	done = 0;
	lane_num = (int)(pConf->lcd_control.mipi_config->lane_num);
	bit_rate_pclk_factor = pConf->lcd_control.mipi_config->bit_rate /
		pConf->lcd_timing.lcd_clk;
	while ((i <= (pConf->lcd_basic.h_active/8)) && (done == 0)) {
		pixel_per_chunk = i * 8;
		if (pConf->lcd_control.mipi_config->dpi_data_format ==
			COLOR_18BIT_CFG_1) {
			/* 18bit (4*18/8=9byte) */
			byte_per_chunk = pixel_per_chunk * 9/4;
		} else {
			/* 24bit or 18bit-loosely */
			byte_per_chunk = pixel_per_chunk * 3;
		}
		total_bytes_per_chunk =
			(lane_num * pixel_per_chunk * bit_rate_pclk_factor) / 8;
		num_of_chunk = pConf->lcd_basic.h_active / pixel_per_chunk;
		/* byte_per_chunk+6=valid_payload */
		chunk_overhead = total_bytes_per_chunk -
			(byte_per_chunk + DSI_PACKET_HEADER_CRC);
		if (chunk_overhead >= DSI_PACKET_HEADER_CRC) {
			/* if room for null_vid's head(4)+crc(2) */
			/* chunk_overhead-null_vid's head(4)+crc(2) =
			    null_vid's payload */
			vid_null_size = chunk_overhead - DSI_PACKET_HEADER_CRC;
			done = 1;
		} else if (chunk_overhead >= 0) {
			vid_null_size = 0;
			done = 1;
		} else {
			vid_null_size = 0;
		}
		i++;
	}
	if (done == 0) {
		DPRINT(" No room packet header & CRC, chunk_overhead is %d\n",
			chunk_overhead);
	}

	dsi_vconf.pixel_per_chunk = pixel_per_chunk;
	dsi_vconf.num_of_chunk = num_of_chunk;
	dsi_vconf.vid_null_size = vid_null_size;
	lcd_print(" ============== NON_BURST SETTINGS =============\n");
	lcd_print(" pixel_per_chunk       = %d\n", pixel_per_chunk);
	lcd_print(" num_of_chunk          = %d\n", num_of_chunk);
	lcd_print(" total_bytes_per_chunk = %d\n", total_bytes_per_chunk);
	lcd_print(" byte_per_chunk        = %d\n", byte_per_chunk);
	lcd_print(" chunk_overhead        = %d\n", chunk_overhead);
	lcd_print(" vid_null_size         = %d\n", vid_null_size);
	lcd_print(" ===============================================\n");
}

static void startup_mipi_dsi_host(void)
{
	lcd_print("%s\n", __func__);

	/* Enable dwc mipi_dsi_host's clock */
	dsi_reg_write(MIPI_DSI_TOP_CNTL,
		(dsi_reg_read(MIPI_DSI_TOP_CNTL) & ~(0x7<<4))
		| (1 << 4) | (1 << 5) | (0 << 6));
	/* mipi_dsi_host's reset */
	dsi_reg_set_mask(MIPI_DSI_TOP_SW_RESET, 0xf);
	/* Release mipi_dsi_host's reset */
	dsi_reg_clr_mask(MIPI_DSI_TOP_SW_RESET, 0xf);
	/* Enable dwc mipi_dsi_host's clock */
	dsi_reg_set_mask(MIPI_DSI_TOP_CLK_CNTL, 0x3);
	mdelay(10);
}

/* *************************************************************
 * Function: set_mipi_dsi_host
 * Parameters: vcid,              // virtual id
		chroma_subsample, // chroma_subsample for YUV422 or YUV420 only
		operation_mode,   // video mode/command mode
		p,                //lcd config
 * ************************************************************* */
static void set_mipi_dsi_host(unsigned int vcid, unsigned int chroma_subsample,
		unsigned int operation_mode, struct Lcd_Config_s *p)
{
	unsigned int dpi_data_format, venc_data_width;
	unsigned int lane_num, vid_mode_type;
	enum tv_enc_lcd_type_e  output_type;
	unsigned int temp;
	struct DSI_Config_s *mconf;

	mconf = p->lcd_control.mipi_config;
	venc_data_width = mconf->venc_data_width;
	dpi_data_format = mconf->dpi_data_format;
	lane_num        = (unsigned int)(mconf->lane_num);
	vid_mode_type   = (unsigned int)(mconf->video_mode_type);
	output_type     = mconf->venc_fmt;

	/* ----------------------------------------------------- */
	/* Standard Configuration for Video Mode Operation */
	/* ----------------------------------------------------- */
	/* 1,    Configure Lane number and phy stop wait time */
	if ((output_type != TV_ENC_LCD240x160_dsi) &&
		(output_type != TV_ENC_LCD1920x1200p) &&
		(output_type != TV_ENC_LCD2560x1600) &&
		(output_type != TV_ENC_LCD768x1024p)) {
		dsi_reg_write(MIPI_DSI_DWC_PHY_IF_CFG_OS,
			(0x28 << BIT_PHY_STOP_WAIT_TIME) |
			((lane_num-1) << BIT_N_LANES));
	} else {
		dsi_reg_write(MIPI_DSI_DWC_PHY_IF_CFG_OS,
			(1 << BIT_PHY_STOP_WAIT_TIME) |
			((lane_num-1) << BIT_N_LANES));
	}

	/* 2.1,  Configure Virtual channel settings */
	dsi_reg_write(MIPI_DSI_DWC_DPI_VCID_OS, vcid);
	/* 2.2,  Configure Color format */
	dsi_reg_write(MIPI_DSI_DWC_DPI_COLOR_CODING_OS,
		(((dpi_data_format == COLOR_18BIT_CFG_2) ?
			1 : 0) << BIT_LOOSELY18_EN) |
		(dpi_data_format << BIT_DPI_COLOR_CODING));
	/* 2.2.1 Configure Set color format for DPI register */
	temp = (dsi_reg_read(MIPI_DSI_TOP_CNTL) &
		~(0xf<<BIT_DPI_COLOR_MODE) &
		~(0x7<<BIT_IN_COLOR_MODE) &
		~(0x3<<BIT_CHROMA_SUBSAMPLE));
	dsi_reg_write(MIPI_DSI_TOP_CNTL,
		(temp |
		(dpi_data_format  << BIT_DPI_COLOR_MODE)  |
		(venc_data_width  << BIT_IN_COLOR_MODE)   |
		(chroma_subsample << BIT_CHROMA_SUBSAMPLE)));
	/* 2.3   Configure Signal polarity */
	dsi_reg_write(MIPI_DSI_DWC_DPI_CFG_POL_OS,
		(0x0 << BIT_COLORM_ACTIVE_LOW) |
		(0x0 << BIT_SHUTD_ACTIVE_LOW)  |
		/* (((p->lcd_timing.pol_ctrl >> POL_CTRL_HS) & 1) <<
			BIT_HSYNC_ACTIVE_LOW)  | */
		(0 << BIT_HSYNC_ACTIVE_LOW)    |
		/* (((p->lcd_timing.pol_ctrl >> POL_CTRL_VS) & 1) <<
			BIT_VSYNC_ACTIVE_LOW)  | */
		(0 << BIT_VSYNC_ACTIVE_LOW)    |
		(0x0 << BIT_DATAEN_ACTIVE_LOW));

	if (operation_mode == OPERATION_VIDEO_MODE) {
		/* 3.1   Configure Low power and video mode type settings */
		dsi_reg_write(MIPI_DSI_DWC_VID_MODE_CFG_OS,
			(1 << BIT_LP_HFP_EN)  |       /* enalbe lp */
			(1 << BIT_LP_HBP_EN)  |       /* enalbe lp */
			(1 << BIT_LP_VCAT_EN) |       /* enalbe lp */
			(1 << BIT_LP_VFP_EN)  |       /* enalbe lp */
			(1 << BIT_LP_VBP_EN)  |       /* enalbe lp */
			(1 << BIT_LP_VSA_EN)  |       /* enalbe lp */
			(1 << BIT_FRAME_BTA_ACK_EN) | /* enable BTA after one
						frame, TODO, need check */
			/* (1 << BIT_LP_CMD_EN) |     // enable the command
						transmission only in lp mode */
			(vid_mode_type << BIT_VID_MODE_TYPE));  /* burst/
						non burst mode */
		/* [23:16]outvact, [7:0]invact */
		dsi_reg_write(MIPI_DSI_DWC_DPI_LP_CMD_TIM_OS,
			(4 << 16) | (4 << 0));

		/* 3.2   Configure video packet size settings */
		if (vid_mode_type == BURST_MODE) {
			/* should be one line in pixels, such as 480/240... */
			dsi_reg_write(MIPI_DSI_DWC_VID_PKT_SIZE_OS,
				p->lcd_basic.h_active);
		} else {  /* non-burst mode */
			/* in unit of pixels,
			    (pclk period/byte clk period)*num_of_lane
			     should be integer */
			dsi_reg_write(MIPI_DSI_DWC_VID_PKT_SIZE_OS,
				dsi_vconf.pixel_per_chunk);
		}

		/* 3.3   Configure number of chunks and null packet size
				for one line */
		if (vid_mode_type == BURST_MODE) {
			dsi_reg_write(MIPI_DSI_DWC_VID_NUM_CHUNKS_OS, 0);
			dsi_reg_write(MIPI_DSI_DWC_VID_NULL_SIZE_OS, 0);
		} else {  /* non burst mode */
			/* HACT/VID_PKT_SIZE */
			dsi_reg_write(MIPI_DSI_DWC_VID_NUM_CHUNKS_OS,
				dsi_vconf.num_of_chunk);
			/* video null size */
			dsi_reg_write(MIPI_DSI_DWC_VID_NULL_SIZE_OS,
				dsi_vconf.vid_null_size);
		}

		/* 4     Configure the video relative parameters according to
			   the output type */
		/* include horizontal timing and vertical line */
		dsi_reg_write(MIPI_DSI_DWC_VID_HLINE_TIME_OS, dsi_vconf.hline);
		dsi_reg_write(MIPI_DSI_DWC_VID_HSA_TIME_OS, dsi_vconf.hsa);
		dsi_reg_write(MIPI_DSI_DWC_VID_HBP_TIME_OS, dsi_vconf.hbp);
		dsi_reg_write(MIPI_DSI_DWC_VID_VSA_LINES_OS, dsi_vconf.vsa);
		dsi_reg_write(MIPI_DSI_DWC_VID_VBP_LINES_OS, dsi_vconf.vbp);
		dsi_reg_write(MIPI_DSI_DWC_VID_VFP_LINES_OS, dsi_vconf.vfp);
		dsi_reg_write(MIPI_DSI_DWC_VID_VACTIVE_LINES_OS,
			dsi_vconf.vact);
	}  /* operation_mode == OPERATION_VIDEO_MODE */

	/* ----------------------------------------------------- */
	/* Finish Configuration */
	/* ----------------------------------------------------- */

	/* Inner clock divider settings */
	dsi_reg_write(MIPI_DSI_DWC_CLKMGR_CFG_OS,
		(0x1 << BIT_TO_CLK_DIV) |
		(dsi_phy_config.lp_tesc << BIT_TX_ESC_CLK_DIV));
	/* Packet header settings  //move to set_mipi_dcs */
	/* dsi_reg_write( MIPI_DSI_DWC_PCKHDL_CFG_OS,
		(1 << BIT_CRC_RX_EN) |
		(1 << BIT_ECC_RX_EN) |
		(0 << BIT_BTA_EN) |
		(0 << BIT_EOTP_RX_EN) |
		(0 << BIT_EOTP_TX_EN) ); */
	/* operation mode setting: video/command mode */
	dsi_reg_write(MIPI_DSI_DWC_MODE_CFG_OS, operation_mode);

	/* Phy Timer */
	if ((output_type != TV_ENC_LCD240x160_dsi) &&
		(output_type != TV_ENC_LCD1920x1200p) &&
		(output_type != TV_ENC_LCD2560x1600) &&
		(output_type != TV_ENC_LCD768x1024p)) {
		dsi_reg_write(MIPI_DSI_DWC_PHY_TMR_CFG_OS, 0x03320000);
	} else {
		dsi_reg_write(MIPI_DSI_DWC_PHY_TMR_CFG_OS, 0x090f0000);
	}

	/* Configure DPHY Parameters */
	if ((output_type != TV_ENC_LCD240x160_dsi) &&
		(output_type != TV_ENC_LCD1920x1200p) &&
		(output_type != TV_ENC_LCD2560x1600) &&
		(output_type != TV_ENC_LCD768x1024p)) {
		dsi_reg_write(MIPI_DSI_DWC_PHY_TMR_LPCLK_CFG_OS, 0x870025);
	} else {
		dsi_reg_write(MIPI_DSI_DWC_PHY_TMR_LPCLK_CFG_OS, 0x260017);
	}
}

static void startup_transfer_cmd(void)
{
	/* Startup transfer */
	dsi_reg_write(MIPI_DSI_DWC_LPCLK_CTRL_OS,
		(0x1 << BIT_AUTOCLKLANE_CTRL) | (0x1 << BIT_TXREQUESTCLKHS));
}
static void startup_transfer_video(void)
{
	dsi_reg_write(MIPI_DSI_DWC_LPCLK_CTRL_OS, (0x1 << BIT_TXREQUESTCLKHS));
}

static void mipi_dsi_host_config(struct Lcd_Config_s *pConf)
{
	unsigned int op_mode_init;

	op_mode_init = ((pConf->lcd_control.mipi_config->operation_mode >>
		BIT_OP_MODE_INIT) & 1);

	if (lcd_print_flag > 0) {
		print_info();
		print_dphy_info();
	}

	lcd_print("Set mipi_dsi_host\n");
	set_mipi_dcs(MIPI_DSI_CMD_TRANS_TYPE, /* 0: high speed, 1: low power */
		MIPI_DSI_DCS_ACK_TYPE,        /* if need bta ack check */
		MIPI_DSI_TEAR_SWITCH);        /* enable tear ack */

	set_mipi_dsi_host(MIPI_DSI_VIRTUAL_CHAN_ID,   /* Virtual channel id */
		0, /* Chroma sub sample, only for YUV 422 or 420, even or odd */
		op_mode_init, /* DSI operation mode, video or command */
		pConf);
}

void mipi_dsi_link_on(struct Lcd_Config_s *pConf)
{
	unsigned int      op_mode_disp, op_mode_init;
	struct DSI_Config_s *mconf;
#ifdef CONFIG_AML_LCD_EXTERN
	struct aml_lcd_extern_driver_s *lcd_ext;
#endif
	unsigned int temp = 0;

	DPRINT("%s\n", __func__);
	mconf = pConf->lcd_control.mipi_config;
	op_mode_disp = ((mconf->operation_mode >> BIT_OP_MODE_DISP) & 1);
	op_mode_init = ((mconf->operation_mode >> BIT_OP_MODE_INIT) & 1);

	if (mconf->lcd_extern_init > 0) {
#ifdef CONFIG_AML_LCD_EXTERN
		lcd_ext = aml_lcd_extern_get_driver();
		if (lcd_ext == NULL) {
			DPRINT("no lcd_extern driver\n");
		} else {
			if (lcd_ext->init_on_cmd_8) {
				temp += dsi_write_cmd(lcd_ext->init_on_cmd_8);
				DPRINT("[extern]%s dsi init on\n",
					lcd_ext->name);
			}
		}
#endif
	}

	if (mconf->dsi_init_on) {
		temp += dsi_write_cmd(mconf->dsi_init_on);
		lcd_print("dsi init on\n");
	}

	if (temp == 0) {
		DPRINT("[warning]: no init command for mipi-dsi, ");
		DPRINT("use default command\n");
		dsi_write_cmd(dsi_init_on_table_dft);
	}

	if (op_mode_disp != op_mode_init) {
		set_mipi_dsi_host(MIPI_DSI_VIRTUAL_CHAN_ID,
			0,  /* Chroma sub sample, only for
				YUV 422 or 420, even or odd */
			op_mode_disp, /* DSI operation mode, video or command */
			pConf);
	}
}

void mipi_dsi_link_off(struct Lcd_Config_s *pConf)
{
#ifdef CONFIG_AML_LCD_EXTERN
	struct aml_lcd_extern_driver_s *lcd_ext;
#endif

	if (pConf->lcd_control.mipi_config->dsi_init_off) {
		dsi_write_cmd(pConf->lcd_control.mipi_config->dsi_init_off);
		lcd_print("dsi init off\n");
	}

	if (pConf->lcd_control.mipi_config->lcd_extern_init > 0) {
#ifdef CONFIG_AML_LCD_EXTERN
		lcd_ext = aml_lcd_extern_get_driver();
		if (lcd_ext == NULL) {
			DPRINT("no lcd_extern driver\n");
		} else {
			if (lcd_ext->init_off_cmd_8) {
				dsi_write_cmd(lcd_ext->init_off_cmd_8);
				DPRINT("[extern]%s dsi init off\n",
					lcd_ext->name);
			}
		}
#endif
	}
}

void set_mipi_dsi_control_config(struct Lcd_Config_s *pConf)
{
	unsigned int pclk, bit_rate, lcd_bits;
	/* unit in kHz for calculation */
	unsigned int bit_rate_max, bit_rate_min, pll_out_fmin;
	unsigned int operation_mode;
	struct DSI_Config_s *mconf = pConf->lcd_control.mipi_config;
	struct lcd_clk_config_s *cConf;
	int n;
	unsigned int temp;

	/* unit in kHz for calculation */
	cConf = get_lcd_clk_config();
	pll_out_fmin = cConf->pll_out_fmin;
	pclk = pConf->lcd_timing.lcd_clk / 1000;
	operation_mode = ((mconf->operation_mode >> BIT_OP_MODE_DISP) & 1);
	mconf->video_mode_type = MIPI_DSI_VIDEO_MODE_TYPE;
	if (pConf->lcd_basic.lcd_bits == 6) {
		mconf->venc_data_width = MIPI_DSI_VENC_COLOR_18B;
		mconf->dpi_data_format = MIPI_DSI_COLOR_18BIT;
		if (mconf->dpi_data_format == COLOR_18BIT_CFG_2)
			lcd_bits = 8;
		else
			lcd_bits = 6;
	} else {
		mconf->venc_data_width = MIPI_DSI_VENC_COLOR_24B;
		mconf->dpi_data_format  = MIPI_DSI_COLOR_24BIT;
		lcd_bits = 8;
	}
	if (mconf->bit_rate_max == 0) { /* auto calculate */
		if ((operation_mode == OPERATION_VIDEO_MODE) &&
			(mconf->video_mode_type != BURST_MODE)) {
			temp = pclk * 4 * lcd_bits;
			bit_rate = temp / mconf->lane_num;
		} else {
			temp = pclk * 3 * lcd_bits;
			bit_rate = temp / mconf->lane_num;
		}
		n = 0;
		bit_rate_min = 0;
		bit_rate_max = 0;
		while ((bit_rate_min < pll_out_fmin) && (n < 100)) {
			bit_rate_max = bit_rate + (pclk / 2) + (n * pclk);
			bit_rate_min = bit_rate_max - pclk;
			n++;
		}
		mconf->bit_rate_max = bit_rate_max / 1000; /* unit: MHz*/
		if (mconf->bit_rate_max > MIPI_PHY_CLK_MAX)
			mconf->bit_rate_max = MIPI_PHY_CLK_MAX;

		DPRINT("mipi dsi bit_rate max=%dMHz\n", mconf->bit_rate_max);
	} else { /* user define */
		if (mconf->bit_rate_max < pll_out_fmin / 1000) {
			DPRINT("[error]: mipi-dsi can't support bit_rate ");
			DPRINT("%dMHz (min=%dMHz)\n",
				mconf->bit_rate_max, (pll_out_fmin / 1000));
		}
		if (mconf->bit_rate_max > MIPI_PHY_CLK_MAX) {
			DPRINT("[warning]: mipi-dsi bit_rate_max %dMHz ",
				mconf->bit_rate_max);
			DPRINT("is out of standard (%dMHz)\n",
				MIPI_PHY_CLK_MAX);
		}
	}

	/* Venc resolution format */
	switch ((mconf->transfer_ctrl >> BIT_TRANS_CTRL_SWITCH) & 3) {
	case 1: /* standard */
		mconf->venc_fmt = TV_ENC_LCD768x1024p;
		break;
	case 2: /* slow */
		mconf->venc_fmt = TV_ENC_LCD1280x720;
		break;
	case 0: /* auto */
	default:
		if ((pConf->lcd_basic.h_active != 240) &&
			(pConf->lcd_basic.h_active != 768) &&
			(pConf->lcd_basic.h_active != 1920) &&
			(pConf->lcd_basic.h_active != 2560))
			mconf->venc_fmt = TV_ENC_LCD1280x720;
		else
			mconf->venc_fmt = TV_ENC_LCD768x1024p;
		break;
	}
}

void set_mipi_dsi_control_config_post(struct Lcd_Config_s *pConf)
{
	unsigned int pclk, lanebyteclk;
	unsigned int operation_mode;
	unsigned int den, num;
	struct DSI_Config_s *mconf = pConf->lcd_control.mipi_config;

	pclk = pConf->lcd_timing.lcd_clk;

	if (mconf->factor_numerator == 0) {
		lanebyteclk = mconf->bit_rate / 8;
		lcd_print("pixel_clk = %d.%03dMHz, bit_rate = %d.%03dMHz, ",
			(pclk / 1000000), ((pclk / 1000) % 1000),
			(mconf->bit_rate / 1000000),
			((mconf->bit_rate / 1000) % 1000));
		lcd_print("lanebyteclk = %d.%03dMHz\n",
			(lanebyteclk / 1000000), ((lanebyteclk / 1000) % 1000));

		mconf->factor_denominator = lanebyteclk/1000;
		mconf->factor_numerator = pclk/1000;
		/* mconf->factor_denominator = 10; */
	}
	den = mconf->factor_denominator;
	num = mconf->factor_numerator;
	lcd_print("den=%d, num=%d, factor=%d.%02d\n", den, num,
		(den / num), ((den % num) * 100 / num));

	operation_mode = ((mconf->operation_mode >> BIT_OP_MODE_DISP) & 1);
	if (operation_mode == OPERATION_VIDEO_MODE) {
		dsi_video_config(pConf);
		if (mconf->video_mode_type != BURST_MODE)
			dsi_non_burst_chunk_config(pConf);
	}

	set_dsi_phy_config(&dsi_phy_config, mconf->bit_rate);
}

void mipi_dsi_host_on(struct Lcd_Config_s *pConf)
{
	unsigned int transfer_ctrl;

	startup_mipi_dsi_host();
	mipi_dsi_host_config(pConf);
	mipi_dsi_phy_config(pConf);
	transfer_ctrl = pConf->lcd_control.mipi_config->transfer_ctrl;
	if (((transfer_ctrl >> BIT_TRANS_CTRL_CLK) & 1) == 0)
		startup_transfer_video();
	else
		startup_transfer_cmd();
}

void mipi_dsi_host_off(void)
{
	lcd_print("poweroff dsi digital\n");
	/* Power down DSI */
	dsi_reg_write(MIPI_DSI_DWC_PWR_UP_OS, 0);

	/* Power down D-PHY, do not have to close dphy */
	/* dsi_reg_write(MIPI_DSI_DWC_PHY_RSTZ_OS,
		(dsi_reg_read( MIPI_DSI_DWC_PHY_RSTZ_OS ) & 0xc)); */
	/* dsi_reg_write(MIPI_DSI_DWC_PHY_RSTZ_OS, 0xc); */

	dsi_phy_write(MIPI_DSI_CHAN_CTRL, 0x1f);
	lcd_print("MIPI_DSI_PHY_CTRL=0x%x\n", dsi_phy_read(MIPI_DSI_PHY_CTRL));
	dsi_phy_setb(MIPI_DSI_PHY_CTRL, 0, 7, 1);
}

/* *********************************************** */
static const char *dsi_usage_str = {
"Usage:\n"
"    echo read <addr> <reg_count> > dsi ; read dsi phy reg value\n"
"    echo write <addr> <value> > dsi ; write dsi phy reg with value\n"
"    echo info > dsi ; print dsi config information\n"
"    echo dphy > dsi ; print dsi phy timing information\n"
"    echo cmd <data_type> <data_num> <data_1> <data_2> ... > dsi ; write single dsi command\n"
"        <data_num>: decimal number\n"
"        <data_n>:   variable length, total length must equal to <data_num>. no need '0xff,0xff' ending flag"
};

static ssize_t dsi_debug_help(struct class *class,
		struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", dsi_usage_str);
}

static ssize_t dsi_debug(struct class *class,
		struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int ret;
	unsigned t[4];
	unsigned char cmd[6];
	unsigned num = 0;
	int i;

	if (lcd_chip_valid_check() == FALSE)
		return -EINVAL;
	switch (buf[0]) {
	case 'r': /* read */
		num = 1;
		t[0] = 0;
		ret = sscanf(buf, "read %x %u", &t[0], &num);
		DPRINT("read dsi phy reg:\n");
		for (i = 0; i < num; i++) {
			DPRINT("  0x%04x = 0x%08x\n",
				(t[0]+i), dsi_phy_read((t[0]+i)));
		}
		break;
	case 'w': /* write */
		t[0] = 0;
		t[1] = 0;
		ret = sscanf(buf, "write %x %x", &t[0], &t[1]);
		dsi_phy_write(t[0], t[1]);
		DPRINT("write dsi phy reg 0x%04x = 0x%08x, readback 0x%08x\n",
			t[0], t[1], dsi_phy_read(t[0]));
		break;
	case 'i':
		print_info();
		break;
	case 'd':
		print_dphy_info();
		break;
	case 'c':
		t[0] = 0xff;
		t[1] = 0xff;
		t[2] = 0;
		t[3] = 0;
		ret = sscanf(buf, "cmd %x %d %x %x",
			&t[0], &t[1], &t[2], &t[3]);
		cmd[0] = (unsigned char)(t[0] & 0xff);
		cmd[1] = (unsigned char)(t[1] & 0xff);
		cmd[2] = (unsigned char)(t[2] & 0xff);
		cmd[3] = (unsigned char)(t[3] & 0xff);
		cmd[4] = 0xff;
		cmd[5] = 0xff;
		dsi_write_cmd(&cmd[0]);
		break;
	default:
		DPRINT("wrong format of dsi debug command\n");
		break;
	}

	if (ret != 1 || ret != 2)
		return -EINVAL;

	return count;
	/* return 0; */
}

static struct class_attribute dsi_debug_class_attrs[] = {
	__ATTR(dsi, S_IRUGO | S_IWUSR, dsi_debug_help, dsi_debug),
};

int creat_dsi_attr(struct class *debug_class)
{
	int i;

	/* create class attr */
	for (i = 0; i < ARRAY_SIZE(dsi_debug_class_attrs); i++) {
		if (class_create_file(debug_class, &dsi_debug_class_attrs[i])) {
			DPRINT("create dsi debug attribute %s fail\n",
				dsi_debug_class_attrs[i].attr.name);
		}
	}

	return 0;
}
int remove_dsi_attr(struct class *debug_class)
{
	int i;

	if (debug_class == NULL)
		return -1;

	for (i = 0; i < ARRAY_SIZE(dsi_debug_class_attrs); i++)
		class_remove_file(debug_class, &dsi_debug_class_attrs[i]);

	return 0;
}
/* ********************************************************* */

void dsi_probe(struct Lcd_Config_s *pConf)
{
	dConf = pConf->lcd_control.mipi_config;
}

void dsi_remove(void)
{
	dConf = NULL;
}

