/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Author:	Torsten Schenk <torsten.schenk@zoho.com>
 * Created:	Jan 01, 2011
 * Copyright:	(C) Torsten Schenk
 *
 * Thanks to:
 * - Holger Ruckdeschel: he found out how to control individual analog
 *   channel volumes and introduced mute switches
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef USB6FIRE_CONTROL_H
#define USB6FIRE_CONTROL_H

#include "common.h"

struct control_runtime {
	int (*update_streaming)(struct control_runtime *rt);
	int (*set_rate)(struct control_runtime *rt, unsigned int rate);

	struct sfire_chip *chip;

	bool opt_coax_switch;
	bool line_phono_switch;
	bool digital_thru_switch;
	bool usb_streaming;
	bool use_analog;
	bool use_spdif;

	u8 output_vol[6];
	u8 output_mute;
	s8 input_vol[2];
};

int usb6fire_control_init(struct sfire_chip *chip);
void usb6fire_control_abort(struct sfire_chip *chip);
void usb6fire_control_destroy(struct sfire_chip *chip);
#endif /* USB6FIRE_CONTROL_H */

