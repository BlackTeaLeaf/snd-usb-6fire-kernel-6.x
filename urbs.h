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

#ifndef USB6FIRE_URBS_H
#define USB6FIRE_URBS_H

#include <linux/atomic.h>

#include "common.h"
#include "substream.h"

enum {
	URBS_COUNT = 4
};

enum {
	URBS_SUBSTATE_DISABLED,
	URBS_SUBSTATE_SUSPENDED,
	URBS_SUBSTATE_ENABLED
};

struct urbs_substream {
	struct snd_pcm_substream *alsa_sub;
	spinlock_t lock;
	int state;
	bool active;
	bool trigger_active;

	snd_pcm_uframes_t dma_off;
	snd_pcm_uframes_t dma_size;
	snd_pcm_uframes_t period_off;
	snd_pcm_uframes_t period_size;
	atomic_t dma_off_public;
	bool in;
	int max_channels;
	int channels;
	struct substream_copier *copier; /* size: max_channels */
	struct substream_muter *muter; /* size: max_channels */
	struct substream_resetter resetter;
	struct substream_copier (*get_copier)(struct substream_runtime *rt, struct substream_descriptor *descriptor, int channel);
};

struct urbs_queue {
	struct urbs_runtime *urbs;

	atomic_t head;
	atomic_t tail;
	struct urb *node[URBS_COUNT + 1]; /* +1: full/empty distinction */
};

struct urbs_runtime {
	struct sfire_chip *chip;

	struct tasklet_struct tasklet;
	bool tasklet_enabled;
	bool use_tasklet;

	struct urb *in_urbs[URBS_COUNT];
	struct urb *out_urbs[URBS_COUNT];
	struct urbs_queue in_queue;
	struct urbs_queue out_queue;
	struct urbs_substream substreams[SUBSTREAM_MAX_COUNT];
	int n_substreams;
	
	atomic_t state;
	unsigned int rate;
	unsigned int packets_per_urb;

	int (*set_state)(struct urbs_runtime *rt, struct substream_descriptor *desc, int state); /* not thread safe lock externally */
	int (*get_min_period_size)(struct urbs_runtime *rt, unsigned int rate);
	int (*set_active)(struct urbs_runtime *rt, struct substream_descriptor *desc, bool active); /* thread safe */
	snd_pcm_uframes_t (*get_pointer)(struct urbs_runtime *rt, struct substream_descriptor *desc); /* thread safe */
	
	int in_frame_size;
	int out_frame_size;
	wait_queue_head_t stream_wait_queue;
};

int usb6fire_urbs_init(struct sfire_chip *chip);
void usb6fire_urbs_abort(struct sfire_chip *chip);
void usb6fire_urbs_destroy(struct sfire_chip *chip);

#endif /* USB6FIRE_URBS_H */

