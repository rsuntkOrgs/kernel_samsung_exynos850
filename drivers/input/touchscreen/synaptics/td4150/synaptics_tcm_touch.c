/*
 * Synaptics TCM touchscreen driver
 *
 * Copyright (C) 2017-2018 Synaptics Incorporated. All rights reserved.
 *
 * Copyright (C) 2017-2018 Scott Lin <scott.lin@tw.synaptics.com>
 * Copyright (C) 2018-2019 Ian Su <ian.su@tw.synaptics.com>
 * Copyright (C) 2018-2019 Joey Zhou <joey.zhou@synaptics.com>
 * Copyright (C) 2018-2019 Yuehao Qiu <yuehao.qiu@synaptics.com>
 * Copyright (C) 2018-2019 Aaron Chen <aaron.chen@tw.synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include "synaptics_tcm_core.h"

#define TYPE_B_PROTOCOL

#define USE_DEFAULT_TOUCH_REPORT_CONFIG

#define TOUCH_REPORT_CONFIG_SIZE 128

enum touch_status {
	LIFT = 0,
	FINGER = 1,
	GLOVED_FINGER = 2,
	NOP = -1,
};

enum gesture_id {
	NO_GESTURE_DETECTED = 0,
	GESTURE_DOUBLE_TAP = 0X01,
};

enum display_deep_sleep_state_id {
	SLEEP_NO_CHANGE = 0,
	SLEEP_IN = 1,
	SLEEP_OUT = 2,
};

enum touch_report_code {
	TOUCH_END = 0,
	TOUCH_FOREACH_ACTIVE_OBJECT,
	TOUCH_FOREACH_OBJECT,
	TOUCH_FOREACH_END,
	TOUCH_PAD_TO_NEXT_BYTE,
	TOUCH_TIMESTAMP,
	TOUCH_OBJECT_N_INDEX,
	TOUCH_OBJECT_N_CLASSIFICATION,
	TOUCH_OBJECT_N_X_POSITION,
	TOUCH_OBJECT_N_Y_POSITION,
	TOUCH_OBJECT_N_Z,
	TOUCH_OBJECT_N_X_WIDTH,
	TOUCH_OBJECT_N_Y_WIDTH,
	TOUCH_OBJECT_N_TX_POSITION_TIXELS,
	TOUCH_OBJECT_N_RX_POSITION_TIXELS,
	TOUCH_0D_BUTTONS_STATE,
	TOUCH_GESTURE_ID,
	TOUCH_FRAME_RATE,
	TOUCH_POWER_IM,
	TOUCH_CID_IM,
	TOUCH_RAIL_IM,
	TOUCH_CID_VARIANCE_IM,
	TOUCH_NSM_FREQUENCY,
	TOUCH_NSM_STATE,
	TOUCH_NUM_OF_ACTIVE_OBJECTS,
	TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME,
	TOUCH_FACE_DETECT,
	TOUCH_GESTURE_DATA,
	TOUCH_OBJECT_N_FORCE,
	TOUCH_FINGERPRINT_AREA_MEET,
	TOUCH_TUNING_GAUSSIAN_WIDTHS = 0x80,
	TOUCH_TUNING_SMALL_OBJECT_PARAMS,
	TOUCH_TUNING_0D_BUTTONS_VARIANCE,
	TOUCH_REPORT_DISPLAY_DEEP_SLEEP_STATE = 0xC0,
};

struct object_data {
	unsigned char status;
	unsigned int x_pos;
	unsigned int y_pos;
	unsigned int x_width;
	unsigned int y_width;
	unsigned int z;
	unsigned int tx_pos;
	unsigned int rx_pos;
};

struct input_params {
	unsigned int max_x;
	unsigned int max_y;
	unsigned int max_objects;
};

struct touch_data {
	struct object_data *object_data;
	unsigned int timestamp;
	unsigned int buttons_state;
	unsigned int gesture_id;
	unsigned int frame_rate;
	unsigned int power_im;
	unsigned int cid_im;
	unsigned int rail_im;
	unsigned int cid_variance_im;
	unsigned int nsm_frequency;
	unsigned int nsm_state;
	unsigned int num_of_active_objects;
	unsigned int num_of_cpu_cycles;
	unsigned int fd_data;
	unsigned int force_data;
	unsigned int fingerprint_area_meet;
	unsigned int display_deep_sleep_state;
};

struct touch_hcd {
	bool irq_wake;
	bool init_touch_ok;
	unsigned char *prev_status;
	unsigned int max_x;
	unsigned int max_y;
	unsigned int max_objects;
	struct mutex report_mutex;
	struct input_dev *input_dev;
	struct input_dev *input_dev_proximity;
	struct input_dev *input_dev_dexpad;
	struct touch_data touch_data;
	struct input_params input_params;
	struct syna_tcm_buffer out;
	struct syna_tcm_buffer resp;
	struct syna_tcm_hcd *tcm_hcd;
};

static struct touch_hcd *touch_hcd;

/**
 * touch_free_objects() - Free all touch objects
 *
 * Report finger lift events to the input subsystem for all touch objects.
 */
void touch_free_objects(void)
{
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
#ifdef TYPE_B_PROTOCOL
	unsigned int idx;
#endif

	if (touch_hcd->input_dev == NULL)
		return;

	mutex_lock(&touch_hcd->report_mutex);

	if (tcm_hcd->prox_power_off) {
		input_info(true, tcm_hcd->pdev->dev.parent, "%s cancel touch\n", __func__);
		input_report_key(touch_hcd->input_dev, KEY_INT_CANCEL, 1);
		input_sync(touch_hcd->input_dev);
		input_report_key(touch_hcd->input_dev, KEY_INT_CANCEL, 0);
		input_sync(touch_hcd->input_dev);
	}

#ifdef TYPE_B_PROTOCOL
	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		input_mt_slot(touch_hcd->input_dev, idx);
		input_report_abs(touch_hcd->input_dev, ABS_MT_PRESSURE, 0);
		input_mt_report_slot_state(touch_hcd->input_dev, MT_TOOL_FINGER, 0);
	}
#endif
	input_report_key(touch_hcd->input_dev, BTN_TOUCH, 0);
	input_report_key(touch_hcd->input_dev, BTN_TOOL_FINGER, 0);
#ifndef TYPE_B_PROTOCOL
	input_mt_sync(touch_hcd->input_dev);
#endif
	input_sync(touch_hcd->input_dev);

	touch_hcd->tcm_hcd->finger_state[idx] = 0;

	mutex_unlock(&touch_hcd->report_mutex);

	return;
}

/************************************************************
*  720  * 1480 : <48 96 60> indicator: 24dp navigator:48dp edge:60px dpi=320
* 1080  * 2220 :  4096 * 4096 : <133 266 341>  (approximately value)
************************************************************/
void synap_location_detect(struct syna_tcm_hcd *tcm_hcd, char *loc, int x, int y)
{
	int i;

	for (i = 0; i < LOCATION_DETECT_SIZE; ++i)
		loc[i] = 0;

	if (x < tcm_hcd->hw_if->bdata->area_edge)
		strcat(loc, "E.");
	else if (x < (touch_hcd->max_x - tcm_hcd->hw_if->bdata->area_edge))
		strcat(loc, "C.");
	else
		strcat(loc, "e.");

	if (y < tcm_hcd->hw_if->bdata->area_indicator)
		strcat(loc, "S");
	else if (y < (touch_hcd->max_y - tcm_hcd->hw_if->bdata->area_navigation))
		strcat(loc, "C");
	else
		strcat(loc, "N");
}

/**
 * touch_get_report_data() - Retrieve data from touch report
 *
 * Retrieve data from the touch report based on the bit offset and bit length
 * information from the touch report configuration.
 */
static int touch_get_report_data(unsigned int offset,
		unsigned int bits, unsigned int *data)
{
	unsigned char mask;
	unsigned char byte_data;
	unsigned int output_data;
	unsigned int bit_offset;
	unsigned int byte_offset;
	unsigned int data_bits;
	unsigned int available_bits;
	unsigned int remaining_bits;
	unsigned char *touch_report;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	if (bits == 0 || bits > 32) {
		input_err(true, tcm_hcd->pdev->dev.parent, "Invalid number of bits\n");
		return -EINVAL;
	}

	if (offset + bits > tcm_hcd->report.buffer.data_length * 8) {
		*data = 0;
		return 0;
	}

	touch_report = tcm_hcd->report.buffer.buf;

	output_data = 0;
	remaining_bits = bits;

	bit_offset = offset % 8;
	byte_offset = offset / 8;

	while (remaining_bits) {
		byte_data = touch_report[byte_offset];
		byte_data >>= bit_offset;

		available_bits = 8 - bit_offset;
		data_bits = MIN(available_bits, remaining_bits);
		mask = 0xff >> (8 - data_bits);

		byte_data &= mask;

		output_data |= byte_data << (bits - remaining_bits);

		bit_offset = 0;
		byte_offset += 1;
		remaining_bits -= data_bits;
	}

	*data = output_data;

	return 0;
}

/**
 * touch_parse_report() - Parse touch report
 *
 * Traverse through the touch report configuration and parse the touch report
 * generated by the device accordingly to retrieve the touch data.
 */
static int touch_parse_report(void)
{
	int retval;
	bool active_only;
	bool num_of_active_objects;
	unsigned char code;
	unsigned int size;
	unsigned int idx;
	unsigned int obj;
	unsigned int next;
	unsigned int data;
	unsigned int bits;
	unsigned int offset;
	unsigned int objects;
	unsigned int active_objects;
	unsigned int report_size;
	unsigned int config_size;
	unsigned char *config_data;
	struct touch_data *touch_data;
	struct object_data *object_data;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	static unsigned int end_of_foreach;

	touch_data = &touch_hcd->touch_data;
	object_data = touch_hcd->touch_data.object_data;

	config_data = tcm_hcd->config.buf;
	config_size = tcm_hcd->config.data_length;

	report_size = tcm_hcd->report.buffer.data_length;

	size = sizeof(*object_data) * touch_hcd->max_objects;
	memset(touch_hcd->touch_data.object_data, 0x00, size);

	num_of_active_objects = false;

	idx = 0;
	offset = 0;
	objects = 0;
	active_objects = 0;
	active_only = false;
	touch_data->display_deep_sleep_state = SLEEP_NO_CHANGE;

	while (idx < config_size) {
		code = config_data[idx++];

		if (tcm_hcd->lp_state == LP_MODE && tcm_hcd->prox_power_off && code != TOUCH_FACE_DETECT) {
//			input_info(true, tcm_hcd->pdev->dev.parent, "%s: LPM && prox_power_off && !TOUCH_FACE_DETECT\n", __func__);
			bits = config_data[idx++];
			offset += bits;
			continue;
		}

		switch (code) {
		case TOUCH_END:
			goto exit;
		case TOUCH_FOREACH_ACTIVE_OBJECT:
			obj = 0;
			next = idx;
			active_only = true;
			break;
		case TOUCH_FOREACH_OBJECT:
			obj = 0;
			next = idx;
			active_only = false;
			break;
		case TOUCH_FOREACH_END:
			end_of_foreach = idx;
			if (active_only) {
				if (num_of_active_objects) {
					objects++;
					if (objects < active_objects)
						idx = next;
				} else if (offset < report_size * 8) {
					idx = next;
				}
			} else {
				obj++;
				if (obj < touch_hcd->max_objects)
					idx = next;
			}
			break;
		case TOUCH_PAD_TO_NEXT_BYTE:
			offset = ceil_div(offset, 8) * 8;
			break;
		case TOUCH_TIMESTAMP:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get timestamp\n");
				return retval;
			}
			touch_data->timestamp = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_INDEX:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &obj);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object index\n");
				return retval;
			}
			offset += bits;
			break;
		case TOUCH_OBJECT_N_CLASSIFICATION:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object classification\n");
				return retval;
			}
			object_data[obj].status = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_X_POSITION:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object x position\n");
				return retval;
			}
			object_data[obj].x_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Y_POSITION:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object y position\n");
				return retval;
			}
			object_data[obj].y_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Z:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object z\n");
				return retval;
			}
			object_data[obj].z = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_X_WIDTH:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object x width\n");
				return retval;
			}
			object_data[obj].x_width = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Y_WIDTH:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object y width\n");
				return retval;
			}
			object_data[obj].y_width = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_TX_POSITION_TIXELS:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object tx position\n");
				return retval;
			}
			object_data[obj].tx_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_RX_POSITION_TIXELS:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object rx position\n");
				return retval;
			}
			object_data[obj].rx_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_FORCE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object force\n");
				return retval;
			}
			touch_data->force_data = data;
			offset += bits;
			break;
		case TOUCH_FINGERPRINT_AREA_MEET:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get object force\n");
				return retval;
			}
			touch_data->fingerprint_area_meet = data;
			input_dbg(true, tcm_hcd->pdev->dev.parent,
					"fingerprint_area_meet = %x\n",
					touch_data->fingerprint_area_meet);
			offset += bits;
			break;
		case TOUCH_0D_BUTTONS_STATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get 0D buttons state\n");
				return retval;
			}
			touch_data->buttons_state = data;
			offset += bits;
			break;
		case TOUCH_GESTURE_ID:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get gesture double tap\n");
				return retval;
			}
			touch_data->gesture_id = data;
			offset += bits;
			break;
		case TOUCH_FRAME_RATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get frame rate\n");
				return retval;
			}
			touch_data->frame_rate = data;
			offset += bits;
			break;
		case TOUCH_POWER_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get power IM\n");
				return retval;
			}
			touch_data->power_im = data;
			offset += bits;
			break;
		case TOUCH_CID_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get CID IM\n");
				return retval;
			}
			touch_data->cid_im = data;
			offset += bits;
			break;
		case TOUCH_RAIL_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get rail IM\n");
				return retval;
			}
			touch_data->rail_im = data;
			offset += bits;
			break;
		case TOUCH_CID_VARIANCE_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get CID variance IM\n");
				return retval;
			}
			touch_data->cid_variance_im = data;
			offset += bits;
			break;
		case TOUCH_NSM_FREQUENCY:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get NSM frequency\n");
				return retval;
			}
			touch_data->nsm_frequency = data;
			offset += bits;
			break;
		case TOUCH_NSM_STATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get NSM state\n");
				return retval;
			}
			tcm_hcd->noise = touch_data->nsm_state = data;
			offset += bits;
			break;
		case TOUCH_GESTURE_DATA:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_NUM_OF_ACTIVE_OBJECTS:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get number of active objects\n");
				return retval;
			}
			active_objects = data;
			num_of_active_objects = true;
			touch_data->num_of_active_objects = data;
			offset += bits;
			if (touch_data->num_of_active_objects == 0) {
				if (0 == end_of_foreach) {
					input_err(true, tcm_hcd->pdev->dev.parent,
						"Invalid report, num_active and end_foreach are 0\n");
					return 0;
				}
				idx = end_of_foreach;
			}
			break;
		case TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to get num CPU cycles used since last frame\n");
				return retval;
			}
			touch_data->num_of_cpu_cycles = data;
			offset += bits;
			break;
		case TOUCH_FACE_DETECT:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
						"Failed to detect face\n");
				return retval;
			}

			// for debug have to check it!
//			input_info(false, tcm_hcd->pdev->dev.parent, "%s: show all hover %d\n", __func__, data);
			if (tcm_hcd->hw_if->bdata->support_ear_detect) {
				if (touch_data->fd_data != data) {
					input_info(true, tcm_hcd->pdev->dev.parent, "%s: hover %d\n", __func__, data);
					//handle debug value 15
					if(data != 15)
						tcm_hcd->hover_event = data;
					
					input_report_abs(touch_hcd->input_dev_proximity, ABS_MT_CUSTOM, data);
					input_sync(touch_hcd->input_dev_proximity);
				}
			}

			touch_data->fd_data = data;
			offset += bits;
			break;
		case TOUCH_TUNING_GAUSSIAN_WIDTHS:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_TUNING_SMALL_OBJECT_PARAMS:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_TUNING_0D_BUTTONS_VARIANCE:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_REPORT_DISPLAY_DEEP_SLEEP_STATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				input_err(true, tcm_hcd->pdev->dev.parent,
					"Failed to get display deep sleep state\n");
				return retval;
			}

			if (touch_data->display_deep_sleep_state != data)
				input_info(true, tcm_hcd->pdev->dev.parent, "%s: display_deep_sleep_state %d\n", __func__, data);

			touch_data->display_deep_sleep_state = data;
			offset += bits;
			break;
		default:
			bits = config_data[idx++];
			offset += bits;
			break;
		}
	}

exit:
	return 0;
}

/**
 * touch_report() - Report touch events
 *
 * Retrieve data from the touch report generated by the device and report touch
 * events to the input subsystem.
 */
static void touch_report(void)
{
	int retval;
	unsigned int idx;
	unsigned int x;
	unsigned int y;
	unsigned int temp;
	unsigned int status;
	struct touch_data *touch_data;
	struct object_data *object_data;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	const struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	char location[LOCATION_DETECT_SIZE] = { 0, };

	if (!touch_hcd->init_touch_ok)
		return;

	if (touch_hcd->input_dev == NULL)
		return;

	if (tcm_hcd->lp_state == PWR_OFF)
		return;

	mutex_lock(&touch_hcd->report_mutex);

	if (tcm_hcd->lp_state == LP_MODE) {
		pm_wakeup_event(tcm_hcd->pdev->dev.parent, 1000);

		/* waiting for blsp block resuming, if not occurs i2c error */
		retval = wait_for_completion_interruptible_timeout(&tcm_hcd->resume_done, msecs_to_jiffies(500));
		if (retval == 0) {
			input_err(true, tcm_hcd->pdev->dev.parent, "%s: LPM: pm resume is not handled\n", __func__);
			mutex_unlock(&touch_hcd->report_mutex);
			return;
		} else if (retval < 0) {
			input_err(true, tcm_hcd->pdev->dev.parent, "%s: LPM: -ERESTARTSYS if interrupted, %d\n", __func__, retval);
			mutex_unlock(&touch_hcd->report_mutex);
			return;
		}

		input_dbg(true, tcm_hcd->pdev->dev.parent, "%s: run LPM interrupt handler, %d\n", __func__, retval);
		/* run lpm interrupt handler */
	}

	retval = touch_parse_report();
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent,
				"Failed to parse touch report\n");
		goto exit;
	}

	touch_data = &touch_hcd->touch_data;
	object_data = touch_hcd->touch_data.object_data;

#if WAKEUP_GESTURE
	if (touch_data->gesture_id == GESTURE_DOUBLE_TAP &&
			 tcm_hcd->lp_state == LP_MODE && tcm_hcd->wakeup_gesture_enabled) {

		input_err(true, tcm_hcd->pdev->dev.parent, "GESTURE_DOUBLE_TAP\n");
		input_report_key(touch_hcd->input_dev, KEY_WAKEUP, 1);
		input_sync(touch_hcd->input_dev);
		input_report_key(touch_hcd->input_dev, KEY_WAKEUP, 0);
		input_sync(touch_hcd->input_dev);
	}
#endif

	if (tcm_hcd->lp_state == LP_MODE)
		goto exit;

	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		if (touch_hcd->prev_status[idx] == LIFT &&
				object_data[idx].status == LIFT)
			status = NOP;
		else
			status = object_data[idx].status;

		switch (status) {
		case LIFT:
#ifdef TYPE_B_PROTOCOL
			input_mt_slot(touch_hcd->input_dev, idx);
			input_report_abs(touch_hcd->input_dev, ABS_MT_PRESSURE, 0);
			input_mt_report_slot_state(touch_hcd->input_dev, MT_TOOL_FINGER, 0);
#endif
			if (tcm_hcd->touch_count > 0)
				tcm_hcd->touch_count--;
			if (tcm_hcd->touch_count == 0) {
				input_report_key(touch_hcd->input_dev, BTN_TOUCH, 0);
				input_report_key(touch_hcd->input_dev, BTN_TOOL_FINGER, 0);
#ifndef TYPE_B_PROTOCOL
				input_mt_sync(touch_hcd->input_dev);
#endif
			}

			synap_location_detect(tcm_hcd, location, tcm_hcd->finger_pre_x[idx], tcm_hcd->finger_pre_y[idx]);
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
			input_info(true, tcm_hcd->pdev->dev.parent,
				"[R]tID:%d lx:%d ly:%d loc:%s dd:%d,%d mc:%d tc:%d\n",
						idx,tcm_hcd->finger_pre_x[idx], tcm_hcd->finger_pre_y[idx], location,
						tcm_hcd->finger_pre_x[idx] - tcm_hcd->finger_x[idx],
						tcm_hcd->finger_pre_y[idx] - tcm_hcd->finger_y[idx],
						tcm_hcd->mcount[idx], tcm_hcd->touch_count);
#else
			input_info(true, tcm_hcd->pdev->dev.parent,
				"[R]tID:%d loc:%s dd:%d,%d mc:%d tc:%d\n",
						idx, location, tcm_hcd->finger_pre_x[idx] - tcm_hcd->finger_x[idx],
						tcm_hcd->finger_pre_y[idx] - tcm_hcd->finger_y[idx],
						tcm_hcd->mcount[idx], tcm_hcd->touch_count);
#endif
			tcm_hcd->finger_state[idx] = 0;
			tcm_hcd->mcount[idx] = 0;

			break;
		case FINGER:
		case GLOVED_FINGER:
			x = object_data[idx].x_pos;
			y = object_data[idx].y_pos;
			if (bdata->swap_axes) {
				temp = x;
				x = y;
				y = temp;
			}
			if (bdata->x_flip)
				x = touch_hcd->input_params.max_x - x;
			if (bdata->y_flip)
				y = touch_hcd->input_params.max_y - y;

			if (object_data[idx].z == 0)
				object_data[idx].z = 1;

			tcm_hcd->finger_pre_x[idx] = x;
			tcm_hcd->finger_pre_y[idx] = y;
			tcm_hcd->mcount[idx]++;
#ifdef TYPE_B_PROTOCOL
			input_mt_slot(touch_hcd->input_dev, idx);
			input_mt_report_slot_state(touch_hcd->input_dev, MT_TOOL_FINGER, 1);
#endif
			input_report_key(touch_hcd->input_dev, BTN_TOUCH, 1);
			input_report_key(touch_hcd->input_dev, BTN_TOOL_FINGER, 1);
			input_report_abs(touch_hcd->input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(touch_hcd->input_dev, ABS_MT_POSITION_Y, y);
			input_report_abs(touch_hcd->input_dev, ABS_MT_PRESSURE, object_data[idx].z);
#ifndef TYPE_B_PROTOCOL
			input_mt_sync(touch_hcd->input_dev);
#endif

			if (tcm_hcd->finger_state[idx] == 0) {
				tcm_hcd->finger_state[idx] = 1;

				tcm_hcd->touch_count++;

				synap_location_detect(tcm_hcd, location, x, y);
				tcm_hcd->finger_x[idx] = x;
				tcm_hcd->finger_y[idx] = y;
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
				input_info(true, tcm_hcd->pdev->dev.parent,
					"[P]tID:%d.%d x:%d y:%d z:%d loc:%s tc:%d noise:%d\n",
						idx, (touch_hcd->input_dev->mt->trkid - 1) & TRKID_MAX, x, y, object_data[idx].z,
						location, tcm_hcd->touch_count, touch_data->nsm_state);
#else
				input_info(true, tcm_hcd->pdev->dev.parent,
					"[P]tID:%d.%d loc:%s tc:%d noise:%d\n",
						idx, (touch_hcd->input_dev->mt->trkid - 1) & TRKID_MAX, location,
						tcm_hcd->touch_count, touch_data->nsm_state);
#endif
			}

			break;
		default:
			break;
		}

		touch_hcd->prev_status[idx] = object_data[idx].status;
	}

	input_sync(touch_hcd->input_dev);

exit:
	mutex_unlock(&touch_hcd->report_mutex);

	return;
}

/**
 * touch_set_input_params() - Set input parameters
 *
 * Set the input parameters of the input device based on the information
 * retrieved from the application information packet. In addition, set up an
 * array for tracking the status of touch objects.
 */
static int touch_set_input_params(void)
{
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	input_set_abs_params(touch_hcd->input_dev, ABS_MT_POSITION_X, 0, touch_hcd->max_x, 0, 0);
	input_set_abs_params(touch_hcd->input_dev, ABS_MT_POSITION_Y, 0, touch_hcd->max_y, 0, 0);
	input_set_abs_params(touch_hcd->input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);

	input_mt_init_slots(touch_hcd->input_dev, touch_hcd->max_objects, INPUT_MT_DIRECT);

	touch_hcd->input_params.max_x = touch_hcd->max_x;
	touch_hcd->input_params.max_y = touch_hcd->max_y;
	touch_hcd->input_params.max_objects = touch_hcd->max_objects;

	if (touch_hcd->max_objects == 0)
		return 0;

	kfree(touch_hcd->prev_status);
	touch_hcd->prev_status = kzalloc(touch_hcd->max_objects, GFP_KERNEL);
	if (!touch_hcd->prev_status) {
		input_err(true, tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd->prev_status\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * touch_get_input_params() - Get input parameters
 *
 * Retrieve the input parameters to register with the input subsystem for
 * the input device from the application information packet. In addition,
 * the touch report configuration is retrieved and stored.
 */
static int touch_get_input_params(void)
{
	int retval;
	unsigned int temp;
	struct syna_tcm_app_info *app_info;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	const struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;

	app_info = &tcm_hcd->app_info;
	touch_hcd->max_x = le2_to_uint(app_info->max_x);
	touch_hcd->max_y = le2_to_uint(app_info->max_y);
	touch_hcd->max_objects = le2_to_uint(app_info->max_objects);

	if (bdata->swap_axes) {
		temp = touch_hcd->max_x;
		touch_hcd->max_x = touch_hcd->max_y;
		touch_hcd->max_y = temp;
	}

	LOCK_BUFFER(tcm_hcd->config);

	retval = tcm_hcd->write_message(tcm_hcd, CMD_GET_TOUCH_REPORT_CONFIG,
				NULL, 0, &tcm_hcd->config.buf, &tcm_hcd->config.buf_size,
				&tcm_hcd->config.data_length, NULL, 0);
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent,
				"Failed to write command %s\n", STR(CMD_GET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(tcm_hcd->config);
		return retval;
	}

	UNLOCK_BUFFER(tcm_hcd->config);

	return 0;
}

static void touch_set_input_prop_dexpad(struct input_dev *dev)
{
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	static char mms_phys[64] = { 0 };

	snprintf(mms_phys, sizeof(mms_phys), "%s1", TOUCH_INPUT_PHYS_PATH);
	dev->phys = mms_phys;
	dev->dev.parent = tcm_hcd->pdev->dev.parent;

	set_bit(EV_SYN, dev->evbit);
	set_bit(EV_KEY, dev->evbit);
	set_bit(EV_ABS, dev->evbit);
	set_bit(EV_SW, dev->evbit);
	set_bit(BTN_TOUCH, dev->keybit);
	set_bit(BTN_TOOL_FINGER, dev->keybit);
	set_bit(KEY_BLACK_UI_GESTURE, dev->keybit);
	set_bit(KEY_INT_CANCEL, dev->keybit);
	set_bit(INPUT_PROP_POINTER, dev->propbit);
	set_bit(KEY_HOMEPAGE, dev->keybit);

	input_set_abs_params(dev, ABS_MT_POSITION_X, 0, touch_hcd->max_x, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_Y, 0, touch_hcd->max_x, 0, 0);
	input_set_abs_params(dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(dev, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	input_set_abs_params(dev, ABS_MT_CUSTOM, 0, 0xFFFFFFFF, 0, 0);

	input_mt_init_slots(dev, touch_hcd->max_objects, INPUT_MT_POINTER);
	input_set_drvdata(dev, tcm_hcd);
}

static void touch_set_input_prop_proximity(struct input_dev *dev)
{
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	static char mms_phys[64] = { 0 };

	snprintf(mms_phys, sizeof(mms_phys), "%s1", TOUCH_INPUT_PHYS_PATH);
	dev->phys = mms_phys;
	dev->dev.parent = tcm_hcd->pdev->dev.parent;

	set_bit(EV_SYN, dev->evbit);
	set_bit(EV_SW, dev->evbit);
	set_bit(INPUT_PROP_DIRECT, dev->propbit);

	input_set_abs_params(dev, ABS_MT_CUSTOM, 0, 0xFFFFFFFF, 0, 0);
	input_set_drvdata(dev, tcm_hcd);
}

/**
 * touch_set_input_dev() - Set up input device
 *
 * Allocate an input device, configure the input device based on the particular
 * input events to be reported, and register the input device with the input
 * subsystem.
 */
static int touch_set_input_dev(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	touch_hcd->input_dev = input_allocate_device();
	if (touch_hcd->input_dev == NULL) {
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to allocate input device\n");
		return -ENODEV;
	}

	if (tcm_hcd->hw_if->bdata->support_ear_detect) {
		touch_hcd->input_dev_proximity = input_allocate_device();
		if (touch_hcd->input_dev_proximity == NULL) {
			input_err(true, tcm_hcd->pdev->dev.parent, "%s: allocate input_dev_proximity err!\n", __func__);
			if (touch_hcd->input_dev) {
				input_free_device(touch_hcd->input_dev);
				touch_hcd->input_dev = NULL;
				return -ENODEV;
			}
		}

		touch_hcd->input_dev_proximity->name = "sec_touchproximity";
		touch_set_input_prop_proximity(touch_hcd->input_dev_proximity);
	}

	touch_hcd->input_dev_dexpad = input_allocate_device();
	if (touch_hcd->input_dev_dexpad == NULL) {
		input_err(true, tcm_hcd->pdev->dev.parent, "%s: allocate input_dev_dexpad err!\n", __func__);
		if (touch_hcd->input_dev) {
			input_free_device(touch_hcd->input_dev);
			touch_hcd->input_dev = NULL;
			return -ENODEV;
		}
	}

	touch_hcd->input_dev_dexpad->name = "sec_touchpad";
	touch_set_input_prop_dexpad(touch_hcd->input_dev_dexpad);

	touch_hcd->input_dev->name = TOUCH_INPUT_NAME;
	touch_hcd->input_dev->phys = TOUCH_INPUT_PHYS_PATH;
	touch_hcd->input_dev->id.product = SYNAPTICS_TCM_ID_PRODUCT;
	touch_hcd->input_dev->id.version = SYNAPTICS_TCM_ID_VERSION;
	touch_hcd->input_dev->dev.parent = tcm_hcd->pdev->dev.parent;
	input_set_drvdata(touch_hcd->input_dev, tcm_hcd);

	set_bit(EV_SYN, touch_hcd->input_dev->evbit);
	set_bit(EV_KEY, touch_hcd->input_dev->evbit);
	set_bit(EV_ABS, touch_hcd->input_dev->evbit);
	set_bit(BTN_TOUCH, touch_hcd->input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, touch_hcd->input_dev->keybit);
#ifdef INPUT_PROP_DIRECT
	set_bit(INPUT_PROP_DIRECT, touch_hcd->input_dev->propbit);
#endif
	set_bit(KEY_INT_CANCEL, touch_hcd->input_dev->keybit);
#if WAKEUP_GESTURE
	set_bit(KEY_WAKEUP, touch_hcd->input_dev->keybit);
	input_set_capability(touch_hcd->input_dev, EV_KEY, KEY_WAKEUP);
#endif

	retval = touch_set_input_params();
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to set input parameters\n");
		input_free_device(touch_hcd->input_dev);
		if (tcm_hcd->hw_if->bdata->support_ear_detect) {
			if (touch_hcd->input_dev_proximity)
				input_free_device(touch_hcd->input_dev_proximity);
		}
		if (touch_hcd->input_dev_dexpad) {
			input_free_device(touch_hcd->input_dev_dexpad);
		}
		touch_hcd->input_dev = NULL;
		return retval;
	}

	retval = input_register_device(touch_hcd->input_dev);
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to register input device\n");
		input_free_device(touch_hcd->input_dev);
		if (tcm_hcd->hw_if->bdata->support_ear_detect) {
			if (touch_hcd->input_dev_proximity)
				input_free_device(touch_hcd->input_dev_proximity);
		}
		if (touch_hcd->input_dev_dexpad) {
			input_free_device(touch_hcd->input_dev_dexpad);
		}
		touch_hcd->input_dev = NULL;
		return retval;
	}

	if (tcm_hcd->hw_if->bdata->support_ear_detect) {
		retval = input_register_device(touch_hcd->input_dev_proximity);
		if (retval < 0) {
			input_err(true, tcm_hcd->pdev->dev.parent, "%s: Unable to register %s input device\n",
						__func__, touch_hcd->input_dev_proximity->name);

			input_free_device(touch_hcd->input_dev);
			if (tcm_hcd->hw_if->bdata->support_ear_detect) {
				if (touch_hcd->input_dev_proximity)
					input_free_device(touch_hcd->input_dev_proximity);
			}
			input_unregister_device(touch_hcd->input_dev);
			touch_hcd->input_dev = NULL;
			return retval;
		}
	}

	retval = input_register_device(touch_hcd->input_dev_dexpad);
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent, "%s: Unable to register %s input device\n",
					__func__, touch_hcd->input_dev_dexpad->name);

		input_free_device(touch_hcd->input_dev);
		if (touch_hcd->input_dev_dexpad)
			input_free_device(touch_hcd->input_dev_dexpad);
			
		input_unregister_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
		return retval;
	}

	return 0;
}

/**
 * touch_set_report_config() - Set touch report configuration
 *
 * Send the SET_TOUCH_REPORT_CONFIG command to configure the format and content
 * of the touch report.
 */
static int touch_set_report_config(void)
{
	int retval;
	unsigned int idx;
	unsigned int length;
	struct syna_tcm_app_info *app_info;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

#ifdef USE_DEFAULT_TOUCH_REPORT_CONFIG
	return 0;
#endif

	app_info = &tcm_hcd->app_info;
	length = le2_to_uint(app_info->max_touch_report_config_size);

	if (length < TOUCH_REPORT_CONFIG_SIZE) {
		input_err(true, tcm_hcd->pdev->dev.parent,
				"Invalid maximum touch report config size\n");
		return -EINVAL;
	}

	LOCK_BUFFER(touch_hcd->out);

	retval = syna_tcm_alloc_mem(tcm_hcd,
			&touch_hcd->out,
			length);
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd->out.buf\n");
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	idx = 0;
#if WAKEUP_GESTURE
	touch_hcd->out.buf[idx++] = TOUCH_GESTURE_ID;
	touch_hcd->out.buf[idx++] = 8;
#endif
	touch_hcd->out.buf[idx++] = TOUCH_FACE_DETECT;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_DISPLAY_DEEP_SLEEP_STATE;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_ACTIVE_OBJECT;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_INDEX;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_CLASSIFICATION;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_POSITION;
	touch_hcd->out.buf[idx++] = 12;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_POSITION;
	touch_hcd->out.buf[idx++] = 12;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_END;
	touch_hcd->out.buf[idx++] = TOUCH_END;

	LOCK_BUFFER(touch_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd, CMD_SET_TOUCH_REPORT_CONFIG,
				touch_hcd->out.buf,	length,	&touch_hcd->resp.buf,
				&touch_hcd->resp.buf_size, &touch_hcd->resp.data_length,
				NULL, 0);
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent,
				"Failed to write command %s\n", STR(CMD_SET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(touch_hcd->resp);
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	UNLOCK_BUFFER(touch_hcd->resp);
	UNLOCK_BUFFER(touch_hcd->out);

	input_dbg(true, tcm_hcd->pdev->dev.parent, "Set touch config done\n");

	return 0;
}

/**
 * touch_check_input_params() - Check input parameters
 *
 * Check if any of the input parameters registered with the input subsystem for
 * the input device has changed.
 */
static int touch_check_input_params(void)
{
	unsigned int size;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	if (touch_hcd->max_x == 0 && touch_hcd->max_y == 0)
		return 0;

	if (touch_hcd->input_params.max_objects != touch_hcd->max_objects) {
		kfree(touch_hcd->touch_data.object_data);
		size = sizeof(*touch_hcd->touch_data.object_data);
		size *= touch_hcd->max_objects;
		touch_hcd->touch_data.object_data = kzalloc(size, GFP_KERNEL);
		if (!touch_hcd->touch_data.object_data) {
			input_err(true, tcm_hcd->pdev->dev.parent,
					"Failed to allocate memory for touch_hcd->touch_data.object_data\n");
			return -ENOMEM;
		}
		return 1;
	}

	if (touch_hcd->input_params.max_x != touch_hcd->max_x)
		return 1;

	if (touch_hcd->input_params.max_y != touch_hcd->max_y)
		return 1;

	return 0;
}

/**
 * touch_set_input_reporting() - Configure touch report and set up new input
 * device if necessary
 *
 * After a device reset event, configure the touch report and set up a new input
 * device if any of the input parameters has changed after the device reset.
 */
static int touch_set_input_reporting(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	if (IS_NOT_FW_MODE(tcm_hcd->id_info.mode) || tcm_hcd->app_status != APP_STATUS_OK) {
		input_dbg(true, tcm_hcd->pdev->dev.parent,
				"Identifying mode = 0x%02x\n", tcm_hcd->id_info.mode);
		return 0;
	}

	touch_hcd->init_touch_ok = false;

	touch_free_objects();

	mutex_lock(&touch_hcd->report_mutex);

	retval = touch_set_report_config();
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to set report config\n");
		goto exit;
	}

	retval = touch_get_input_params();
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to get input parameters\n");
		goto exit;
	}

	retval = touch_check_input_params();
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to check input parameters\n");
		goto exit;
	} else if (retval == 0) {
		input_dbg(true, tcm_hcd->pdev->dev.parent, "Input parameters unchanged\n");
		goto exit;
	}

	if (touch_hcd->input_dev != NULL) {
		input_unregister_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
	}

	retval = touch_set_input_dev();
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to set up input device\n");
		goto exit;
	}

exit:
	mutex_unlock(&touch_hcd->report_mutex);

	touch_hcd->init_touch_ok = retval < 0 ? false : true;

	return retval;
}


int touch_init(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;

	touch_hcd = kzalloc(sizeof(*touch_hcd), GFP_KERNEL);
	if (!touch_hcd) {
		input_err(true, tcm_hcd->pdev->dev.parent,
			"Failed to allocate memory for touch_hcd\n");
		return -ENOMEM;
	}

	touch_hcd->tcm_hcd = tcm_hcd;

	mutex_init(&touch_hcd->report_mutex);

	INIT_BUFFER(touch_hcd->out, false);
	INIT_BUFFER(touch_hcd->resp, false);

	retval = touch_set_input_reporting();
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent,
				"Failed to set up input reporting\n");
		goto err_set_input_reporting;
	}

	tcm_hcd->report_touch = touch_report;
	tcm_hcd->touch_count = 0;

#ifdef CONFIG_SEC_FACTORY
	retval = tcm_hcd->set_dynamic_config(tcm_hcd, DC_ENABLE_EDGE_REJECT, 1);
	if (retval < 0)
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to enable edge reject\n");
	else
		input_info(true, tcm_hcd->pdev->dev.parent, "enable edge reject\n");
#endif
	return 0;

err_set_input_reporting:
	kfree(touch_hcd->touch_data.object_data);
	kfree(touch_hcd->prev_status);

	RELEASE_BUFFER(touch_hcd->resp);
	RELEASE_BUFFER(touch_hcd->out);

	kfree(touch_hcd);
	touch_hcd = NULL;

	return retval;
}

int touch_remove(struct syna_tcm_hcd *tcm_hcd)
{
	if (!touch_hcd)
		goto exit;

	tcm_hcd->report_touch = NULL;

	if (touch_hcd->input_dev)
		input_unregister_device(touch_hcd->input_dev);

	if (tcm_hcd->hw_if->bdata->support_ear_detect) {
		input_mt_destroy_slots(touch_hcd->input_dev_proximity);
		input_unregister_device(touch_hcd->input_dev_proximity);
	}

	kfree(touch_hcd->touch_data.object_data);
	kfree(touch_hcd->prev_status);

	RELEASE_BUFFER(touch_hcd->resp);
	RELEASE_BUFFER(touch_hcd->out);

	kfree(touch_hcd);
	touch_hcd = NULL;

exit:

	return 0;
}

int touch_reinit(struct syna_tcm_hcd *tcm_hcd)
{
	int retval = 0;

	if (!touch_hcd) {
		retval = touch_init(tcm_hcd);
		return retval;
	}

	touch_free_objects();

	if (IS_NOT_FW_MODE(tcm_hcd->id_info.mode)) {
		input_err(true, tcm_hcd->pdev->dev.parent,
				"Application mode is not running (firmware mode = %d)\n",
				tcm_hcd->id_info.mode);
		return 0;
	}

	retval = tcm_hcd->identify(tcm_hcd, false);
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to do identification\n");
		return retval;
	}

	retval = touch_set_input_reporting();
	if (retval < 0) {
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to set up input reporting\n");
	}

	return retval;
}

#if 0
int touch_early_suspend(struct syna_tcm_hcd *tcm_hcd)
{
	if (!touch_hcd)
		return 0;

	touch_free_objects();

	return 0;
}

int touch_suspend(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;

	if (!touch_hcd)
		return 0;

	if (tcm_hcd->wakeup_gesture_enabled) {
		if (!touch_hcd->irq_wake) {
			enable_irq_wake(tcm_hcd->irq);
			touch_hcd->irq_wake = true;
		}

		retval = tcm_hcd->set_dynamic_config(tcm_hcd,
				DC_IN_WAKEUP_GESTURE_MODE,
				1);
		if (retval < 0) {
			input_err(true, tcm_hcd->pdev->dev.parent,
					"Failed to enable wakeup gesture mode\n");
			touch_free_objects();
			return retval;
		}
	}

	touch_free_objects();

	return 0;
}

int touch_resume(struct syna_tcm_hcd *tcm_hcd)
{
#ifdef CONFIG_SEC_FACTORY
	int retval;
#endif
	if (!touch_hcd)
		return 0;

	if (tcm_hcd->wakeup_gesture_enabled) {
		if (touch_hcd->irq_wake) {
			disable_irq_wake(tcm_hcd->irq);
			touch_hcd->irq_wake = false;
		}

/*		retval = tcm_hcd->set_dynamic_config(tcm_hcd, DC_IN_WAKEUP_GESTURE_MODE, 0);
		if (retval < 0) {
			input_err(true, tcm_hcd->pdev->dev.parent,
					"Failed to disable wakeup gesture mode\n");
			return retval;
		}*/
	}

#ifdef CONFIG_SEC_FACTORY
	retval = tcm_hcd->set_dynamic_config(tcm_hcd, DC_ENABLE_EDGE_REJECT, 1);
	if (retval < 0)
		input_err(true, tcm_hcd->pdev->dev.parent, "Failed to enable edge reject\n");
	else
		input_info(true, tcm_hcd->pdev->dev.parent, "enable edge reject\n");
#endif
	return 0;
}
#endif
