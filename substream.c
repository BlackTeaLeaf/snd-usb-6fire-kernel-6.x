/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Stream manager: specifies available streams in alsa
 * and on the device
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

#include "substream.h"
#include "chip.h"
#include "rates.h"

const char parity_table[256] = {
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0
};

enum {
	ANALOG_OUT_N_CHANNELS = 6,
	ANALOG_IN_N_CHANNELS = 4,
	SPDIF_OUT_N_CHANNELS = 2,
	SPDIF_IN_N_CHANNELS = 2,
	OUT_N_CHANNELS = ANALOG_OUT_N_CHANNELS + SPDIF_OUT_N_CHANNELS,
	IN_N_CHANNELS = ANALOG_IN_N_CHANNELS
};

static const int spdif_channel_bits_bm[][10] = {
	{2, 20, 32, 33, 35, -1}, //44.1 kHz
	{2, 20, 25, 32, 33, 35, -1}, //48 kHz
	{2, 20, 27, 32, 33, 35, -1}, //88.2 kHz
	{2, 20, 25, 27, 32, 33, 35, -1} //96 kHz
};

static const int spdif_channel_bits_w[][10] = {
	{2, 21, 32, 33, 35, -1}, //44.1 kHz
	{2, 21, 25, 32, 33, 35, -1}, //48 kHz
	{2, 21, 27, 32, 33, 35, -1}, //88.2 kHz
	{2, 21, 25, 27, 32, 33, 35, -1} //96 kHz
};

static const u64 analog_formats =
	SNDRV_PCM_FMTBIT_S16_LE |
	SNDRV_PCM_FMTBIT_S24_LE |
	SNDRV_PCM_FMTBIT_S32_LE;
static const u64 spdif_formats =
	SNDRV_PCM_FMTBIT_S16_LE |
	SNDRV_PCM_FMTBIT_S24_LE |
	SNDRV_PCM_FMTBIT_S32_LE;

static void usb6fire_substream_a2d_s16le(struct substream_copier *copier, u8 *di, int frame_count)
{
	u8 *si = copier->dma_ptr;

	di += copier->urb_frame_offset;
	while (frame_count--) {
		di[0] = 0x00;
		di[1] = si[0];
		di[2] = si[1];
		di[3] = 0x40;
		si += copier->dma_frame_size;
		if (si == copier->dma_end)
			si = copier->dma_begin;
		di += copier->urb_frame_size;
	}
	copier->dma_ptr = si;
}

static void usb6fire_substream_a2d_s24le(struct substream_copier *copier, u8 *di, int frame_count)
{
	u8 *si = copier->dma_ptr;

	di += copier->urb_frame_offset;
	while (frame_count--) {
		di[0] = si[0];
		di[1] = si[1];
		di[2] = si[2];
		di[3] = 0x40;
		si += copier->dma_frame_size;
		if (si == copier->dma_end)
			si = copier->dma_begin;
		di += copier->urb_frame_size;
	}
	copier->dma_ptr = si;
}

static void usb6fire_substream_a2d_s32le(struct substream_copier *copier, u8 *di, int frame_count)
{
	u8 *si = copier->dma_ptr;

	di += copier->urb_frame_offset;
	while (frame_count--) {
		di[0] = si[1];
		di[1] = si[2];
		di[2] = si[3];
		di[3] = 0x40;
		si += copier->dma_frame_size;
		if (si == copier->dma_end)
			si = copier->dma_begin;
		di += copier->urb_frame_size;
	}
	copier->dma_ptr = si;
}

static void usb6fire_substream_d2a_s16le(struct substream_copier *copier, u8 *si, int frame_count)
{
	u8 *di = copier->dma_ptr;

	si += copier->urb_frame_offset;
	while (frame_count--) {
		di[0] = si[1];
		di[1] = si[2];
		di += copier->dma_frame_size;
		if (di == copier->dma_end)
			di = copier->dma_begin;
		si += copier->urb_frame_size;
	}
	copier->dma_ptr = di;
}

static void usb6fire_substream_d2a_s24le(struct substream_copier *copier, u8 *si, int frame_count)
{
	u8 *di = copier->dma_ptr;

	si += copier->urb_frame_offset;
	while (frame_count--) {
		di[0] = si[0];
		di[1] = si[1];
		di[2] = si[2];
		di[3] = 0x00;
		di += copier->dma_frame_size;
		if (di == copier->dma_end)
			di = copier->dma_begin;
		si += copier->urb_frame_size;
	}
	copier->dma_ptr = di;
}

static void usb6fire_substream_d2a_s32le(struct substream_copier *copier, u8 *si, int frame_count)
{
	u8 *di = copier->dma_ptr;

	si += copier->urb_frame_offset;
	while (frame_count--) {
		di[0] = 0x00;
		di[1] = si[0];
		di[2] = si[1];
		di[3] = si[2];
		di += copier->dma_frame_size;
		if (di == copier->dma_end)
			di = copier->dma_begin;
		si += copier->urb_frame_size;
	}
	copier->dma_ptr = di;
}

static void usb6fire_substream_s2d_s16le(struct substream_copier *copier, u8 *di, int frame_count)
{
	u8 *si = copier->dma_ptr;
	struct substream_spdif_data *spdif_data = copier->user;

	di += copier->urb_frame_offset;
	while (frame_count--) {
		di[0] = 0x00;
		di[1] = si[0];
		di[2] = si[1];
		di[3] = 0x00;
		di[3] |= spdif_data->channel_bits[spdif_data->frame_index];
		di[3] |= parity_table[di[0] ^ di[1] ^ di[2] ^ di[3]];
		if (spdif_data->frame_index)
			di[3] |= spdif_data->bmw[1];
		else
			di[3] |= spdif_data->bmw[0];
		spdif_data->frame_index++;
		spdif_data->frame_index %= 192;
		
		si += copier->dma_frame_size;
		if (si == copier->dma_end)
			si = copier->dma_begin;
		di += copier->urb_frame_size;
	}
	copier->dma_ptr = si;
}

static void usb6fire_substream_s2d_s24le(struct substream_copier *copier, u8 *di, int frame_count)
{
	u8 *si = copier->dma_ptr;
	struct substream_spdif_data *spdif_data = copier->user;

	di += copier->urb_frame_offset;
	while (frame_count--) {
		di[0] = si[0];
		di[1] = si[1];
		di[2] = si[2];
		di[3] = 0x00;
		di[3] |= spdif_data->channel_bits[spdif_data->frame_index];
		di[3] |= parity_table[di[0] ^ di[1] ^ di[2] ^ di[3]];
		if (likely(spdif_data->frame_index))
			di[3] |= spdif_data->bmw[1];
		else
			di[3] |= spdif_data->bmw[0];
		spdif_data->frame_index++;
		spdif_data->frame_index %= 192;
		
		si += copier->dma_frame_size;
		if (si == copier->dma_end)
			si = copier->dma_begin;
		di += copier->urb_frame_size;
	}
	copier->dma_ptr = si;
}

static void usb6fire_substream_s2d_s32le(struct substream_copier *copier, u8 *di, int frame_count)
{
	u8 *si = copier->dma_ptr;
	struct substream_spdif_data *spdif_data = copier->user;

	di += copier->urb_frame_offset;
	while (frame_count--) {
		di[0] = si[1];
		di[1] = si[2];
		di[2] = si[3];
		di[3] = 0x00;
		di[3] |= spdif_data->channel_bits[spdif_data->frame_index];
		di[3] |= parity_table[di[0] ^ di[1] ^ di[2] ^ di[3]];
		if (likely(spdif_data->frame_index))
			di[3] |= spdif_data->bmw[1];
		else
			di[3] |= spdif_data->bmw[0];
		spdif_data->frame_index++;
		spdif_data->frame_index %= 192;

		si += copier->dma_frame_size;
		if (si == copier->dma_end)
			si = copier->dma_begin;
		di += copier->urb_frame_size;
	}
	copier->dma_ptr = si;
}

static void usb6fire_substream_ad_mute(struct substream_muter *muter, u8 *di, int frame_count)
{
	di += muter->urb_frame_offset;
	while (frame_count--) {
		di[0] = 0x00;
		di[1] = 0x00;
		di[2] = 0x00;
		di[3] = 0x40;
		di += muter->urb_frame_size;
	}
}

static void usb6fire_substream_a_mute(struct substream_muter *muter, u8 *si, int frame_count)
{}

static void usb6fire_substream_sd_mute(struct substream_muter *muter, u8 *di, int frame_count)
{
	di += muter->urb_frame_offset;
	while (frame_count--) {
		di[0] = 0xff;
		di[1] = 0xff;
		di[2] = 0xff;
		di[3] = 0xff;
		di += muter->urb_frame_size;
	}
}

static void usb6fire_substream_ad_reset(struct substream_resetter *resetter, unsigned int sample_rate)
{}

static void usb6fire_substream_a_reset(struct substream_resetter *resetter, unsigned int sample_rate)
{}

static void usb6fire_substream_sd_reset(struct substream_resetter *resetter, unsigned int sample_rate)
{
	struct substream_spdif_data *spdif_data = resetter->user;
	int rate_id;
	int i;

	
	spdif_data[0].bmw[0] = 0x30;
	spdif_data[0].bmw[1] = 0x10;
	spdif_data[1].bmw[0] = 0x00;
	spdif_data[1].bmw[1] = 0x00;
	spdif_data[0].frame_index = 0;
	spdif_data[1].frame_index = 0;
	memset(spdif_data[0].channel_bits, 0, 192);
	memset(spdif_data[1].channel_bits, 0, 192);
	rate_id = rate_to_id(sample_rate);
	if (rates_spdif_possible[rate_id]) {
		for(i = 0; spdif_channel_bits_bm[rate_id][i] != -1; i++)
			spdif_data[0].channel_bits[spdif_channel_bits_bm[rate_id][i]] = 0x04;
		for(i = 0; spdif_channel_bits_w[rate_id][i] != -1; i++)
			spdif_data[1].channel_bits[spdif_channel_bits_w[rate_id][i]] = 0x04;
	}
}

static struct substream_copier usb6fire_substream_get_a2d_copier(struct substream_runtime *rt, struct substream_descriptor *desc, int channel)
{
	struct substream_copier copier;
	struct snd_pcm_runtime *alsa_rt = desc->alsa_sub->runtime;

	copier.dma_begin = desc->alsa_sub->runtime->dma_area;
	copier.dma_end = copier.dma_begin + desc->alsa_sub->runtime->dma_bytes;
	copier.dma_begin += channel * desc->alsa_sub->runtime->sample_bits / 8;
	copier.dma_end += channel * desc->alsa_sub->runtime->sample_bits / 8;
	copier.dma_ptr = copier.dma_begin;

	copier.user = NULL;
	copier.urb_frame_size = rt->out_urb_frame_size;
	copier.urb_frame_offset = channel * 4;
	copier.dma_frame_size = desc->alsa_sub->runtime->frame_bits / 8;

	if (alsa_rt->format == SNDRV_PCM_FORMAT_S16_LE)
		copier.exec = usb6fire_substream_a2d_s16le;
	else if (alsa_rt->format == SNDRV_PCM_FORMAT_S24_LE)
		copier.exec = usb6fire_substream_a2d_s24le;
	else if (alsa_rt->format == SNDRV_PCM_FORMAT_S32_LE)
		copier.exec = usb6fire_substream_a2d_s32le;
	else
		copier.exec = NULL;
	return copier;
}

static struct substream_copier usb6fire_substream_get_d2a_copier(struct substream_runtime *rt, struct substream_descriptor *desc, int channel)
{
	struct substream_copier copier;
	struct snd_pcm_runtime *alsa_rt = desc->alsa_sub->runtime;

	copier.dma_begin = desc->alsa_sub->runtime->dma_area;
	copier.dma_end = copier.dma_begin + desc->alsa_sub->runtime->dma_bytes;
	copier.dma_begin += channel * desc->alsa_sub->runtime->sample_bits / 8;
	copier.dma_end += channel * desc->alsa_sub->runtime->sample_bits / 8;
	copier.dma_ptr = copier.dma_begin;

	copier.user = NULL;
	copier.urb_frame_size = rt->in_urb_frame_size;
	copier.urb_frame_offset = channel * 4;
	copier.dma_frame_size = desc->alsa_sub->runtime->frame_bits / 8;

	if (alsa_rt->format == SNDRV_PCM_FORMAT_S16_LE)
		copier.exec = usb6fire_substream_d2a_s16le;
	else if (alsa_rt->format == SNDRV_PCM_FORMAT_S24_LE)
		copier.exec = usb6fire_substream_d2a_s24le;
	else if (alsa_rt->format == SNDRV_PCM_FORMAT_S32_LE)
		copier.exec = usb6fire_substream_d2a_s32le;
	else
		copier.exec = NULL;
	return copier;
}

static struct substream_copier usb6fire_substream_get_s2d_copier(struct substream_runtime *rt, struct substream_descriptor *desc, int channel)
{
	struct substream_copier copier;
	struct snd_pcm_runtime *alsa_rt = desc->alsa_sub->runtime;

	copier.dma_begin = desc->alsa_sub->runtime->dma_area;
	copier.dma_end = copier.dma_begin + desc->alsa_sub->runtime->dma_bytes;
	copier.dma_begin += channel * desc->alsa_sub->runtime->sample_bits / 8;
	copier.dma_end += channel * desc->alsa_sub->runtime->sample_bits / 8;
	copier.dma_ptr = copier.dma_begin;

	copier.user = rt->spdif_data + channel;
	copier.urb_frame_size = rt->out_urb_frame_size;
	copier.urb_frame_offset = channel * 4;
	if (PCM_MODE_USES_ANALOG(rt->chip->pcm_mode))
		copier.urb_frame_offset += ANALOG_OUT_N_CHANNELS * 4;
	copier.dma_frame_size = desc->alsa_sub->runtime->frame_bits / 8;

	if (alsa_rt->format == SNDRV_PCM_FORMAT_S16_LE)
		copier.exec = usb6fire_substream_s2d_s16le;
	else if (alsa_rt->format == SNDRV_PCM_FORMAT_S24_LE)
		copier.exec = usb6fire_substream_s2d_s24le;
	else if (alsa_rt->format == SNDRV_PCM_FORMAT_S32_LE)
		copier.exec = usb6fire_substream_s2d_s32le;
	else
		copier.exec = NULL;
	return copier;
}

static struct substream_muter usb6fire_substream_get_ad_muter(struct substream_runtime *rt, int channel)
{
	struct substream_muter muter;

	muter.exec = usb6fire_substream_ad_mute;	
	muter.user = NULL;
	muter.urb_frame_size = rt->out_urb_frame_size;
	muter.urb_frame_offset = channel * 4;
	return muter;
}

static struct substream_muter usb6fire_substream_get_a_muter(struct substream_runtime *rt, int channel)
{
	struct substream_muter muter;
	
	muter.exec = usb6fire_substream_a_mute;
	muter.user = NULL;
	muter.urb_frame_size = rt->in_urb_frame_size;
	muter.urb_frame_offset = channel * 4;
	return muter;
}

static struct substream_muter usb6fire_substream_get_sd_muter(struct substream_runtime *rt, int channel)
{
	struct substream_muter muter;

	muter.exec = usb6fire_substream_sd_mute;	
	muter.user = rt->spdif_data + channel;
	muter.urb_frame_size = rt->out_urb_frame_size;
	muter.urb_frame_offset = channel * 4;
	if (PCM_MODE_USES_ANALOG(rt->chip->pcm_mode))
		muter.urb_frame_offset += ANALOG_OUT_N_CHANNELS * 4;
	return muter;
}

static struct substream_resetter usb6fire_substream_get_ad_resetter(struct substream_runtime *rt)
{
	struct substream_resetter resetter;
	
	resetter.user = NULL;
	resetter.exec = usb6fire_substream_ad_reset;
	return resetter;
}

static struct substream_resetter usb6fire_substream_get_a_resetter(struct substream_runtime *rt)
{
	struct substream_resetter resetter;
	
	resetter.user = NULL;
	resetter.exec = usb6fire_substream_a_reset;
	return resetter;
}

static struct substream_resetter usb6fire_substream_get_sd_resetter(struct substream_runtime *rt)
{
	struct substream_resetter resetter;
	
	resetter.user = rt->spdif_data;
	resetter.exec = usb6fire_substream_sd_reset;
	return resetter;
}

static struct substream_descriptor usb6fire_substream_get_descriptor(
	struct substream_runtime *rt, struct snd_pcm_substream *alsa_sub)
{
	struct substream_descriptor desc;
	int cur_sub = 0;
	int cur_dev = 0;
	unsigned int rate_id;
	
	desc.alsa_sub = alsa_sub;
	
	desc.input = false;
	desc.index = -EINVAL;
	desc.max_channels = 0;
	desc.rate_min = 0;
	desc.rate_max = 0;
	desc.rate_bits = 0;

	for (rate_id = 0; rate_id < N_RATES; rate_id++)
		if (rates_analog_possible[rate_id] && rates_spdif_possible[rate_id]) {
			desc.rate_bits |= rates_alsa_id[rate_id];
			if (desc.rate_min > rates[rate_id] || !desc.rate_min)
				desc.rate_min = rates[rate_id];
			if (desc.rate_max < rates[rate_id] || !desc.rate_max)
				desc.rate_max = rates[rate_id];
		}

	if (PCM_MODE_USES_ANALOG(rt->chip->pcm_mode)) {
		if (alsa_sub->pcm->device == cur_dev++) {
			if (alsa_sub->stream == SNDRV_PCM_STREAM_PLAYBACK) {
				desc.input = false;
				desc.index = cur_sub;
				desc.max_channels = ANALOG_OUT_N_CHANNELS;
			}
			if (alsa_sub->stream == SNDRV_PCM_STREAM_CAPTURE) {
				desc.input = true;
				desc.index = cur_sub + 1;
				desc.max_channels = ANALOG_IN_N_CHANNELS;
			}
			if (PCM_MODE_ALLOWS_ANALOG_ONLY_RATES(rt->chip->pcm_mode))
				for (rate_id = 0; rate_id < N_RATES; rate_id++)
					if (rates_analog_possible[rate_id] && !rates_spdif_possible[rate_id]) {
						desc.rate_bits |= rates_alsa_id[rate_id];
						if (desc.rate_min > rates[rate_id] || !desc.rate_min)
							desc.rate_min = rates[rate_id];
						if (desc.rate_max < rates[rate_id] || !desc.rate_max)
							desc.rate_max = rates[rate_id];
					}
			desc.formats = analog_formats;
		}
		cur_sub += 2;
	}
	if (PCM_MODE_USES_SPDIF(rt->chip->pcm_mode)) {
		if (alsa_sub->pcm->device == cur_dev++) {
			if (alsa_sub->stream == SNDRV_PCM_STREAM_PLAYBACK) {
				desc.input = false;
				desc.index = cur_sub;
				desc.max_channels = SPDIF_OUT_N_CHANNELS;
			}
			if (PCM_MODE_ALLOWS_SPDIF_ONLY_RATES(rt->chip->pcm_mode))
				for (rate_id = 0; rate_id < N_RATES; rate_id++)
					if (rates_spdif_possible[rate_id] && !rates_analog_possible[rate_id]) {
						desc.rate_bits |= rates_alsa_id[rate_id];
						if (desc.rate_min > rates[rate_id] || !desc.rate_min)
							desc.rate_min = rates[rate_id];
						if (desc.rate_max < rates[rate_id] || !desc.rate_max)
							desc.rate_max = rates[rate_id];
					}
			desc.formats = spdif_formats;
		}
		cur_sub++;
	}
	return desc;
}

static struct substream_definition usb6fire_substream_get_definition(struct substream_runtime *rt, int substream)
{
	struct substream_definition def;
	int cur = 0;
	int out_urb_frame_size = 0;
	int in_urb_frame_size = 0;
	
	def.in = false;
	def.max_channels = 0;
	def.sample_size = 0;
	def.urb_frame_offset = 0;
	def.get_copier = NULL;
	def.get_muter = NULL;
	def.get_resetter = NULL;

	if (PCM_MODE_USES_ANALOG(rt->chip->pcm_mode)) {
		if (substream == cur++) {
			def.urb_frame_offset = out_urb_frame_size;
			def.max_channels = ANALOG_OUT_N_CHANNELS;
			def.sample_size = 4;
			def.in = false;
			def.get_copier = usb6fire_substream_get_a2d_copier;
			def.get_muter = usb6fire_substream_get_ad_muter;
			def.get_resetter = usb6fire_substream_get_ad_resetter;
		} else if (substream == cur++) {
			def.urb_frame_offset = in_urb_frame_size;
			def.max_channels = ANALOG_IN_N_CHANNELS;
			def.sample_size = 4;
			def.in = true;
			def.get_copier = usb6fire_substream_get_d2a_copier;
			def.get_muter = usb6fire_substream_get_a_muter;
			def.get_resetter = usb6fire_substream_get_a_resetter;
		}
		out_urb_frame_size += ANALOG_OUT_N_CHANNELS * 4;
		in_urb_frame_size += ANALOG_IN_N_CHANNELS * 4;
	}
	if(PCM_MODE_USES_SPDIF(rt->chip->pcm_mode)) {
		if (substream == cur++) {
			def.urb_frame_offset = out_urb_frame_size;
			def.max_channels = SPDIF_OUT_N_CHANNELS;
			def.sample_size = 4;
			def.in = false;
			def.get_copier = usb6fire_substream_get_s2d_copier;
			def.get_muter = usb6fire_substream_get_sd_muter;
			def.get_resetter = usb6fire_substream_get_sd_resetter;
		}
		out_urb_frame_size += SPDIF_OUT_N_CHANNELS * 4;
	}

	return def;
}

int usb6fire_substream_init(struct sfire_chip *chip)
{
	struct substream_runtime *rt =
		kzalloc(sizeof(struct substream_runtime), GFP_KERNEL);
	int ret;
	struct snd_pcm *device;

	if(!rt)
		return -ENOMEM;
		
	rt->chip = chip;
	rt->get_descriptor = usb6fire_substream_get_descriptor;
	rt->get_definition = usb6fire_substream_get_definition;

	if (PCM_MODE_USES_ANALOG(rt->chip->pcm_mode)) {
		ret = snd_pcm_new(chip->card, "6fire Analog", rt->n_devices,
			1, 1, &device);
		if (ret < 0) {
			kfree(rt);
			printk(KERN_ERR PREFIX "cannot create pcm instance.\n");
			return ret;
		}
		strcpy(device->name, "DMX 6Fire USB Analog");
		rt->devices[rt->n_devices] = device;
		rt->n_devices++;
		rt->n_substreams += 2;
		rt->out_urb_frame_size += ANALOG_OUT_N_CHANNELS * 4;
		rt->in_urb_frame_size += ANALOG_IN_N_CHANNELS * 4;
	}

	if (PCM_MODE_USES_SPDIF(rt->chip->pcm_mode)) {
		ret = snd_pcm_new(chip->card, "6fire Digital Out", rt->n_devices,
			1, 0, &device);
		if (ret < 0) {
			kfree(rt);
			printk(KERN_ERR PREFIX "cannot create pcm instance.\n");
			return ret;
		}
		strcpy(device->name, "DMX 6Fire USB Digital Playback");
		rt->devices[rt->n_devices] = device;
		rt->spdif_data = kzalloc(SPDIF_OUT_N_CHANNELS
			* sizeof(struct substream_spdif_data), GFP_KERNEL);
		rt->n_devices++;
		rt->n_substreams++;
		rt->out_urb_frame_size += SPDIF_OUT_N_CHANNELS * 4;
		if (!PCM_MODE_USES_ANALOG(rt->chip->pcm_mode)) /* we need at least some input data to calculate number of frames for output per isopacket */
			rt->in_urb_frame_size += SPDIF_IN_N_CHANNELS * 8;
	}

	chip->substream = rt;
	
	return 0;
}

void usb6fire_substream_abort(struct sfire_chip *chip)
{}

void usb6fire_substream_destroy(struct sfire_chip *chip)
{
	kfree(chip->substream);
	chip->substream = NULL;
}
