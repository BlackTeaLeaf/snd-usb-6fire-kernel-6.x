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
#ifndef USB6FIRE_SUBSTREAM_H
#define USB6FIRE_SUBSTREAM_H

#include <sound/pcm.h>

#include "common.h"

enum {
	SUBSTREAM_MAX_COUNT = 3,
	SUBSTREAM_MAX_IN_SAMPLES = 4,
	SUBSTREAM_MAX_OUT_SAMPLES = 6,
	SUBSTREAM_DEVICES_MAX = 2
};

struct substream_spdif_data {
	u8 channel_bits[192];
	int frame_index;
	u8 bmw[2];
/*	u8 subcode_bits[1176];
	int subcode_index;*/
};

/* copies samples device<->urb */
struct substream_copier {
	u8 *dma_begin;
	u8 *dma_end;
	u8 *dma_ptr;
	int dma_frame_size;
	int urb_frame_size;
	int urb_frame_offset;

	void *user;

	void (*exec)(struct substream_copier *copier, u8 *buffer, int frame_count);
};

/* mutes samples in an urb */
struct substream_muter {
	void *user;
	int urb_frame_size;
	int urb_frame_offset;

	void (*exec)(struct substream_muter *muter, u8 *buffer, int frame_count);
};

struct substream_resetter {
	void *user;

	void (*exec)(struct substream_resetter *resetter, unsigned int sample_rate);
};

struct substream_descriptor {
	struct snd_pcm_substream *alsa_sub;
	int index;
	bool input;
	int max_channels;
	unsigned int rate_bits;
	unsigned int rate_min;
	unsigned int rate_max;
	u64 formats;
/*	bool analog;*/
};

struct substream_definition {
	bool in;
	int max_channels;
	int sample_size;
	int urb_frame_offset;	

	struct substream_copier (*get_copier)(struct substream_runtime *rt, struct substream_descriptor *descriptor, int channel);
	struct substream_muter (*get_muter)(struct substream_runtime *rt, int channel);
	struct substream_resetter (*get_resetter)(struct substream_runtime *rt);
};

struct substream_runtime {
	struct sfire_chip *chip;
	
	struct snd_pcm *devices[SUBSTREAM_DEVICES_MAX];
	int n_devices;
	int n_substreams;
	int out_urb_frame_size;
	int in_urb_frame_size;

	struct substream_descriptor (*get_descriptor)(struct substream_runtime *rt, struct snd_pcm_substream *alsa_sub);	
	struct substream_definition (*get_definition)(struct substream_runtime *rt, int substream);
	struct substream_spdif_data *spdif_data;
};

int usb6fire_substream_init(struct sfire_chip *chip);
void usb6fire_substream_abort(struct sfire_chip *chip);
void usb6fire_substream_destroy(struct sfire_chip *chip);
#endif /* USB6FIRE_SUBSTREAM_H */

