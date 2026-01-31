/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * ALSA PCM interface.
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

//TODO URL: http://linux.die.net/man/1/iecset http://alsa.opensrc.org/DigitalOut http://lxr.free-electrons.com/source/sound/pci/hda/hda_codec.c#L2705 http://www.epanorama.net/documents/audio/spdif.html http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/include/sound/control.h#L169

#include <sound/pcm.h>
#include "pcm.h"
#include "chip.h"
#include "substream.h"
#include "urbs.h"

enum {
	MAX_BUFSIZE = 1024 * 1024,
	MIN_PERIODS = 2,
	MAX_PERIODS = 64
};

static const struct snd_pcm_hardware pcm_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH,

	.channels_min = 1,
	.period_bytes_max = MAX_BUFSIZE / MIN_PERIODS,
	.periods_min = MIN_PERIODS,
	.buffer_bytes_max = MAX_BUFSIZE,
	.periods_max = MAX_PERIODS
};

static int usb6fire_pcm_rule_period_size(struct snd_pcm_hw_params *params,
	struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *sri = hw_param_interval(params,
		SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *psi = hw_param_interval(params,
		SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
	struct sfire_chip *chip = rule->private;
	struct urbs_runtime *urbs_rt = chip->urbs;
	unsigned int min_size;
	
	if (chip->shutdown || !urbs_rt)
		return -EPIPE;

	min_size = urbs_rt->get_min_period_size(urbs_rt, sri->max);
	if (!min_size)
		return -EINVAL;

	if (psi->min != min_size) {
		psi->min = min_size;
		return 1;
	}
	else
		return 0;
}

static int usb6fire_pcm_open(struct snd_pcm_substream *alsa_sub)
{
	struct snd_pcm_runtime *alsa_rt = alsa_sub->runtime;
	struct sfire_chip *chip = snd_pcm_substream_chip(alsa_sub);
	struct pcm_runtime *rt = chip->pcm;
	struct substream_runtime *sub_rt = chip->substream;
	struct urbs_runtime *urbs_rt = chip->urbs;
	struct substream_descriptor desc;

	if (chip->shutdown || !rt || !sub_rt || !urbs_rt)
		return -EPIPE;
	desc = sub_rt->get_descriptor(sub_rt, alsa_sub);

	mutex_lock(&rt->lock);
	alsa_rt->hw = pcm_hw;
	alsa_rt->hw.channels_max = desc.max_channels;
	alsa_rt->hw.rate_min = desc.rate_min;
	alsa_rt->hw.rate_max = desc.rate_max;
	alsa_rt->hw.rates = desc.rate_bits;
	alsa_rt->hw.formats = desc.formats;
	if (urbs_rt->rate) {
		if (urbs_rt->rate < desc.rate_min || urbs_rt->rate > desc.rate_max)
			return -EBUSY;
		alsa_rt->hw.rate_min = urbs_rt->rate;
		alsa_rt->hw.rate_max = urbs_rt->rate;
		alsa_rt->hw.rates = snd_pcm_rate_to_rate_bit(urbs_rt->rate);
	}
	snd_pcm_hw_rule_add(alsa_sub->runtime, 0,
		SNDRV_PCM_HW_PARAM_PERIOD_TIME,
		usb6fire_pcm_rule_period_size, chip,
		SNDRV_PCM_HW_PARAM_RATE, -1);
	mutex_unlock(&rt->lock);
	return 0;
}

static int usb6fire_pcm_close(struct snd_pcm_substream *alsa_sub)
{
	struct sfire_chip *chip = snd_pcm_substream_chip(alsa_sub);
	struct pcm_runtime *rt = chip->pcm;
	struct substream_runtime *sub_rt = chip->substream;
	struct urbs_runtime *urbs_rt = chip->urbs;
	struct substream_descriptor desc;
	int ret;
	
	if (chip->shutdown || !rt || !sub_rt || !urbs_rt)
		return 0;
	desc = sub_rt->get_descriptor(sub_rt, alsa_sub);

	mutex_lock(&rt->lock);
	ret = urbs_rt->set_state(urbs_rt, &desc, URBS_SUBSTATE_DISABLED);
	mutex_unlock(&rt->lock);
	return ret; 
}

static int usb6fire_pcm_prepare(struct snd_pcm_substream *alsa_sub)
{
	struct sfire_chip *chip = snd_pcm_substream_chip(alsa_sub);
	struct pcm_runtime *rt = chip->pcm;
	struct substream_runtime *sub_rt = chip->substream;
	struct urbs_runtime *urbs_rt = chip->urbs;
	struct substream_descriptor desc;
	int ret;
	
	if (chip->shutdown || !rt || !sub_rt || !urbs_rt)
		return -EPIPE;
	desc = sub_rt->get_descriptor(sub_rt, alsa_sub);

	mutex_lock(&rt->lock);
	ret = urbs_rt->set_state(urbs_rt, &desc, URBS_SUBSTATE_ENABLED);
	mutex_unlock(&rt->lock);
	return ret;
}

static int usb6fire_pcm_trigger(struct snd_pcm_substream *alsa_sub, int cmd)
{
	struct sfire_chip *chip = snd_pcm_substream_chip(alsa_sub);
	struct pcm_runtime *rt = chip->pcm;
	struct substream_runtime *sub_rt = chip->substream;
	struct urbs_runtime *urbs_rt = chip->urbs;
	struct substream_descriptor desc;

	if (chip->shutdown || !rt || !sub_rt || !urbs_rt)
		return -EPIPE;
	desc = sub_rt->get_descriptor(sub_rt, alsa_sub);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		return urbs_rt->set_active(urbs_rt, &desc, true);

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		return urbs_rt->set_active(urbs_rt, &desc, false);

	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t usb6fire_pcm_pointer(
	struct snd_pcm_substream *alsa_sub)
{
	struct sfire_chip *chip = snd_pcm_substream_chip(alsa_sub);
	struct pcm_runtime *rt = chip->pcm;
	struct substream_runtime *sub_rt = chip->substream;
	struct urbs_runtime *urbs_rt = chip->urbs;
	struct substream_descriptor desc;

	if (chip->shutdown || !rt || !sub_rt || !urbs_rt)
		return -EPIPE;
	desc = sub_rt->get_descriptor(sub_rt, alsa_sub);
	return urbs_rt->get_pointer(urbs_rt, &desc);
}

static struct snd_pcm_ops pcm_ops = {
	.open = usb6fire_pcm_open,
	.close = usb6fire_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.prepare = usb6fire_pcm_prepare,
	.trigger = usb6fire_pcm_trigger,
	.pointer = usb6fire_pcm_pointer,
};

int usb6fire_pcm_init(struct sfire_chip *chip)
{
	struct pcm_runtime *rt = kzalloc(sizeof(struct pcm_runtime), GFP_KERNEL);
	struct substream_runtime *sub_rt = chip->substream;
	// int ret;
	int i;
	
	if (!rt)
		return -ENOMEM;
		
	rt->chip = chip;
	
	mutex_init(&rt->lock);

	for (i = 0; i < sub_rt->n_devices; i++) {
		sub_rt->devices[i]->private_data = chip;
		snd_pcm_set_ops(sub_rt->devices[i], SNDRV_PCM_STREAM_PLAYBACK, &pcm_ops);
		snd_pcm_set_ops(sub_rt->devices[i], SNDRV_PCM_STREAM_CAPTURE, &pcm_ops);
		snd_pcm_set_managed_buffer_all(sub_rt->devices[i],
				SNDRV_DMA_TYPE_VMALLOC,
				NULL,
				0, MAX_BUFSIZE);
	}

	chip->pcm = rt;
	return 0;
}

void usb6fire_pcm_abort(struct sfire_chip *chip)
{}

void usb6fire_pcm_destroy(struct sfire_chip *chip)
{
	kfree(chip->pcm);
	chip->pcm = NULL;
}
