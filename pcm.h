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

#ifndef USB6FIRE_PCM_H
#define USB6FIRE_PCM_H

#include "common.h"

#include <linux/mutex.h>

struct pcm_runtime {
	struct sfire_chip *chip;
	struct mutex lock;
};

int usb6fire_pcm_init(struct sfire_chip *chip);
void usb6fire_pcm_abort(struct sfire_chip *chip);
void usb6fire_pcm_destroy(struct sfire_chip *chip);

#endif /* USB6FIRE_PCM_H */

