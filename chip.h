/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Author:	Torsten Schenk <torsten.schenk@zoho.com>
 * Created:	Jan 01, 2011
 * Copyright:	(C) Torsten Schenk
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef USB6FIRE_CHIP_H
#define USB6FIRE_CHIP_H

#include "common.h"

enum {
	PCM_MODE_OFF,
	PCM_MODE_ANALOG_ONLY,
	PCM_MODE_SPDIF_ONLY,
	PCM_MODE_BOTH_RATE_RESTRICT,
/*	PCM_MODE_BOTH,*/
	PCM_MODES,
	PCM_MODE_DEFAULT = PCM_MODE_BOTH_RATE_RESTRICT
};

enum {
	PCM_TASKLET_THRESH_DEFAULT = 128
};

#define PCM_MODE_USES_ANALOG(X) (X == PCM_MODE_ANALOG_ONLY || X == PCM_MODE_BOTH_RATE_RESTRICT/* || X == PCM_MODE_BOTH*/)
#define PCM_MODE_USES_SPDIF(X) (X == PCM_MODE_SPDIF_ONLY || X == PCM_MODE_BOTH_RATE_RESTRICT/* || X == PCM_MODE_BOTH*/)
#define PCM_MODE_ALLOWS_ANALOG_ONLY_RATES(X) (X == PCM_MODE_ANALOG_ONLY/* || X == PCM_MODE_BOTH*/)
#define PCM_MODE_ALLOWS_SPDIF_ONLY_RATES(X) (X == PCM_MODE_SPDIF_ONLY/* || X == PCM_MODE_BOTH*/)

struct sfire_chip {
	struct usb_device *dev;
	struct snd_card *card;
	int intf_count; /* number of registered interfaces */
	int regidx; /* index in module parameter arrays */
	bool shutdown;
	
	int tasklet_thresh;
	int pcm_mode;

	struct midi_runtime *midi;
	struct control_runtime *control;
	struct comm_runtime *comm;
	struct substream_runtime *substream;
	struct urbs_runtime *urbs;
	struct pcm_runtime *pcm;
};
#endif /* USB6FIRE_CHIP_H */

