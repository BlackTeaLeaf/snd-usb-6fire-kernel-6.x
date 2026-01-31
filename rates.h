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

#ifndef USB6FIRE_RATES_H
#define USB6FIRE_RATES_H

#include <sound/pcm.h>

#include "common.h"

/* Rate IDs used throughout driver */
enum {
	RATE_44KHZ,
	RATE_48KHZ,
	RATE_88KHZ,
	RATE_96KHZ,
	RATE_176KHZ,
	RATE_192KHZ,
	N_RATES
};

/* Digital Thru enabled and no pcm streaming? If so, set this rate  */
enum {
	RATE_DIGITAL_THRU_ONLY = RATE_96KHZ
};

/* Supported rates in Hz */
static const unsigned int rates[] =
	{ 44100, 48000, 88200, 96000, 176400, 192000, 0 };
/* Rates that allow digital audio */
static const bool rates_spdif_possible[] =
	{ true, true, true, true, false, false, false };
static const bool rates_analog_possible[] =
	{ true, true, true, true, true, true, false };
static const bool rates_valid[] =
	{ true, true, true, true, true, true, false };
/* Isopacket size for outgoing urbs */
static const unsigned int rates_out_packet_size[] =
	{ 228, 228, 420, 420, 604, 604 };
/* Isopacket size for incoming urbs */
static const unsigned int rates_in_packet_size[] =
	{ 228, 228, 420, 420, 404, 404 };
/* Inferred from rates_x_packet_size: maximum number of frames per isopacket */
static const unsigned int rates_max_fpp[] =
	{ 7, 7, 13, 13, 25, 25 };
/* Altsetting for the rates */
static const int rates_altsetting[] =
	{ 1, 1, 2, 2, 3, 3 };
/* 6fire rate id low byte */
static const u8 rates_6fire_vl[] =
	{ 0x00, 0x01, 0x00, 0x01, 0x00, 0x01 };
/* 6fire rate id high byte */
static const u8 rates_6fire_vh[] =
	{ 0x11, 0x11, 0x10, 0x10, 0x00, 0x00 };
/* 6fire CS42426 functional mode */
static const u8 rates_fmode[] =
	{ 0x00, 0x00, 0x50, 0x50, 0xa0, 0xa0 };
/* 6fire CS42426 clock control */
static const u8 rates_clock[] =
	{ 0x02, 0x02, 0x02, 0x02, 0x22, 0x22 };
/* Alsa rate bit IDs */
static const unsigned int rates_alsa_id[] = {
	SNDRV_PCM_RATE_44100,
	SNDRV_PCM_RATE_48000,
	SNDRV_PCM_RATE_88200,
	SNDRV_PCM_RATE_96000,
	SNDRV_PCM_RATE_176400,
	SNDRV_PCM_RATE_192000
};

static inline unsigned int rate_to_id(unsigned int rate)
{
	unsigned int rate_id;
	for (rate_id = 0; rate_id < N_RATES; rate_id++)
		if (rates[rate_id] == rate)
			break;
	return rate_id;
}

#endif /* USB6FIRE_RATES_H */

