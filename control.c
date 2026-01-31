/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Mixer control
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

#include <linux/interrupt.h>
#include <sound/control.h>
#include <sound/tlv.h>

#include "control.h"
#include "comm.h"
#include "chip.h"
#include "rates.h"

static char *opt_coax_texts[2] = { "Optical", "Coax" };
static char *line_phono_texts[2] = { "Line", "Phono" };

/*
 * data that needs to be sent to device. sets up card internal stuff.
 * values dumped from windows driver and filtered by trial'n'error.
 */
static const struct {
	u8 type;
	u8 reg;
	u8 value;
}
init_data[] = {
	{ 0x12, 0x0e, 0x2f },
	{ 0x22, 0x00, 0x00 }, { 0x20, 0x00, 0x08 }, { 0x22, 0x01, 0x01 },
	{ 0x20, 0x01, 0x08 }, { 0x22, 0x02, 0x00 }, { 0x20, 0x02, 0x08 },
	{ 0x22, 0x03, 0x00 }, { 0x20, 0x03, 0x08 }, { 0x22, 0x04, 0x00 },
	{ 0x20, 0x04, 0x08 }, { 0x22, 0x05, 0x01 }, { 0x20, 0x05, 0x08 },
	{ 0x22, 0x04, 0x01 }, { 0x12, 0x04, 0x00 }, { 0x12, 0x05, 0x00 },
	{ 0x12, 0x23, 0x00 }, { 0x12, 0x06, 0x02 }, { 0x12, 0x03, 0x00 },
	{ 0x12, 0x02, 0x00 }, { 0x22, 0x03, 0x01 },
	{} /* TERMINATING ENTRY */
};

static DECLARE_TLV_DB_MINMAX(tlv_output, -9000, 0);
static DECLARE_TLV_DB_MINMAX(tlv_input, -1500, 1500);

static void usb6fire_control_output_vol_update(struct control_runtime *rt)
{
	struct comm_runtime *comm_rt = rt->chip->comm;
	int i;

	if (comm_rt)
		for (i = 0; i < 6; i++)
			comm_rt->write8(comm_rt, 0x12, 0x0f + i,
				180 - rt->output_vol[i]);
}

static void usb6fire_control_output_mute_update(struct control_runtime *rt)
{
	struct comm_runtime *comm_rt = rt->chip->comm;

	if (comm_rt)
		comm_rt->write8(comm_rt, 0x12, 0x0e, ~rt->output_mute);
}

static void usb6fire_control_input_vol_update(struct control_runtime *rt)
{
	struct comm_runtime *comm_rt = rt->chip->comm;
	int i;

	if (comm_rt)
		for (i = 0; i < 2; i++)
			comm_rt->write8(comm_rt, 0x12, 0x1c + i,
				rt->input_vol[i] & 0x3f);
}

static void usb6fire_control_line_phono_update(struct control_runtime *rt)
{
	struct comm_runtime *comm_rt = rt->chip->comm;
	if (comm_rt) {
		comm_rt->write8(comm_rt, 0x22, 0x02, rt->line_phono_switch);
		comm_rt->write8(comm_rt, 0x21, 0x02, rt->line_phono_switch);
	}
}

static void usb6fire_control_opt_coax_update(struct control_runtime *rt)
{
	struct comm_runtime *comm_rt = rt->chip->comm;
	if (comm_rt) {
		comm_rt->write8(comm_rt, 0x22, 0x00, rt->opt_coax_switch);
		comm_rt->write8(comm_rt, 0x21, 0x00, rt->opt_coax_switch);
	}
}

static int usb6fire_control_set_rate(struct control_runtime *rt, unsigned int rate)
{
	int ret;
	int rate_id;
	struct usb_device *device = rt->chip->dev;
	struct comm_runtime *comm_rt = rt->chip->comm;

	rt->use_analog = false;
	rt->use_spdif = false;
	rate_id = rate_to_id(rate);
	if (!rates_valid[rate_id])
		return -EINVAL;

	ret = usb_set_interface(device, 1, rates_altsetting[rate_id]);
	if (ret < 0)
		return ret;

	/* set soundcard clock */
	ret = comm_rt->write16(comm_rt, 0x02, 0x01, rates_6fire_vl[rate_id],
			rates_6fire_vh[rate_id]);
	if (ret < 0)
		return ret;
	ret = comm_rt->write8(comm_rt, 0x12, 0x03, rates_fmode[rate_id]);
	if (ret < 0)
		return ret;
	ret = comm_rt->write8(comm_rt, 0x12, 0x06, rates_clock[rate_id]);
	if (ret < 0)
		return ret;

	if (PCM_MODE_USES_ANALOG(rt->chip->pcm_mode))
		rt->use_analog = true;
	if (PCM_MODE_USES_SPDIF(rt->chip->pcm_mode)
		&& rates_spdif_possible[rate_id])
		rt->use_spdif = true;
	return 0;
}

static int usb6fire_control_streaming_update(struct control_runtime *rt)
{
	struct comm_runtime *comm_rt = rt->chip->comm;
	int ret;

	if (comm_rt) {
		if (!rt->usb_streaming && rt->digital_thru_switch)
			usb6fire_control_set_rate(rt,
				rates[RATE_DIGITAL_THRU_ONLY]);
		if (rt->usb_streaming) {
			ret = comm_rt->write16(comm_rt, 0x02, 0x02, 0x00, 0x00);
			if (ret < 0)
				return ret;
			ret = comm_rt->write16(comm_rt, 0x02, 0x03, 0x00, 0x00);
			if (ret < 0)
				return ret;

			if (rt->use_analog) {
				ret = comm_rt->write16(comm_rt, 0x02, 0x02, 0x07, 0x03);
				if (ret < 0)
					return ret;
			}
			if (rt->use_spdif) {
				if (rt->use_analog)
					ret = comm_rt->write16(comm_rt, 0x02, 0x03, 0x00, 0x02);
				else
					ret = comm_rt->write16(comm_rt, 0x02, 0x03, 0x00, 0x03);
				if (ret < 0)
					return ret;
			}
		}
		return comm_rt->write16(comm_rt, 0x02, 0x00, 0x00,
			(rt->usb_streaming ? 0x01 : 0x00) |
			(rt->digital_thru_switch ? 0x08 : 0x00));
	}
	return -EINVAL;
}

static int usb6fire_control_output_vol_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 180;
	return 0;
}

static int usb6fire_control_output_vol_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	unsigned int ch = kcontrol->private_value;
	int changed = 0;

	if (ch > 4) {
		printk(KERN_ERR PREFIX "Invalid channel in volume control.");
		return -EINVAL;
	}

	if (rt->output_vol[ch] != ucontrol->value.integer.value[0]) {
		rt->output_vol[ch] = ucontrol->value.integer.value[0];
		changed = 1;
	}
	if (rt->output_vol[ch + 1] != ucontrol->value.integer.value[1]) {
		rt->output_vol[ch + 1] = ucontrol->value.integer.value[1];
		changed = 1;
	}

	if (changed)
		usb6fire_control_output_vol_update(rt);

	return changed;
}

static int usb6fire_control_output_vol_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	unsigned int ch = kcontrol->private_value;

	if (ch > 4) {
		printk(KERN_ERR PREFIX "Invalid channel in volume control.");
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] = rt->output_vol[ch];
	ucontrol->value.integer.value[1] = rt->output_vol[ch + 1];
	return 0;
}

static int usb6fire_control_output_mute_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	unsigned int ch = kcontrol->private_value;
	u8 old = rt->output_mute;
	u8 value = 0;

	if (ch > 4) {
		printk(KERN_ERR PREFIX "Invalid channel in volume control.");
		return -EINVAL;
	}

	rt->output_mute &= ~(3 << ch);
	if (ucontrol->value.integer.value[0])
		value |= 1;
	if (ucontrol->value.integer.value[1])
		value |= 2;
	rt->output_mute |= value << ch;

	if (rt->output_mute != old)
		usb6fire_control_output_mute_update(rt);

	return rt->output_mute != old;
}

static int usb6fire_control_output_mute_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	unsigned int ch = kcontrol->private_value;
	u8 value = rt->output_mute >> ch;

	if (ch > 4) {
		printk(KERN_ERR PREFIX "Invalid channel in volume control.");
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] = 1 & value;
	value >>= 1;
	ucontrol->value.integer.value[1] = 1 & value;

	return 0;
}

static int usb6fire_control_input_vol_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 30;
	return 0;
}

static int usb6fire_control_input_vol_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	int changed = 0;

	if (rt->input_vol[0] != ucontrol->value.integer.value[0]) {
		rt->input_vol[0] = ucontrol->value.integer.value[0] - 15;
		changed = 1;
	}
	if (rt->input_vol[1] != ucontrol->value.integer.value[1]) {
		rt->input_vol[1] = ucontrol->value.integer.value[1] - 15;
		changed = 1;
	}

	if (changed)
		usb6fire_control_input_vol_update(rt);

	return changed;
}

static int usb6fire_control_input_vol_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = rt->input_vol[0] + 15;
	ucontrol->value.integer.value[1] = rt->input_vol[1] + 15;

	return 0;
}

static int usb6fire_control_line_phono_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name,
		line_phono_texts[uinfo->value.enumerated.item]);
	return 0;
}

static int usb6fire_control_line_phono_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	int changed = 0;
	if (rt->line_phono_switch != ucontrol->value.integer.value[0]) {
		rt->line_phono_switch = ucontrol->value.integer.value[0];
		usb6fire_control_line_phono_update(rt);
		changed = 1;
	}
	return changed;
}

static int usb6fire_control_line_phono_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = rt->line_phono_switch;
	return 0;
}

static int usb6fire_control_opt_coax_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name,
		opt_coax_texts[uinfo->value.enumerated.item]);
	return 0;
}

static int usb6fire_control_opt_coax_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	int changed = 0;

	if (rt->opt_coax_switch != ucontrol->value.enumerated.item[0]) {
		rt->opt_coax_switch = ucontrol->value.enumerated.item[0];
		usb6fire_control_opt_coax_update(rt);
		changed = 1;
	}
	return changed;
}

static int usb6fire_control_opt_coax_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = rt->opt_coax_switch;
	return 0;
}

static int usb6fire_control_digital_thru_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	int changed = 0;

	if (rt->digital_thru_switch != ucontrol->value.integer.value[0]) {
		rt->digital_thru_switch = ucontrol->value.integer.value[0];
		usb6fire_control_streaming_update(rt);
		changed = 1;
	}
	return changed;
}

static int usb6fire_control_digital_thru_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = rt->digital_thru_switch;
	return 0;
}


static struct snd_kcontrol_new common_elements[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Digital Thru Playback Route",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = snd_ctl_boolean_mono_info,
		.get = usb6fire_control_digital_thru_get,
		.put = usb6fire_control_digital_thru_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Opt/Coax Capture Route",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = usb6fire_control_opt_coax_info,
		.get = usb6fire_control_opt_coax_get,
		.put = usb6fire_control_opt_coax_put
	},
	{}
};

static struct snd_kcontrol_new analog_vol_elements[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog 1/2 Playback Volume",
		.index = 0,
		.private_value = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.info = usb6fire_control_output_vol_info,
		.get = usb6fire_control_output_vol_get,
		.put = usb6fire_control_output_vol_put,
		.tlv = { .p = tlv_output }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog 3/4 Playback Volume",
		.index = 0,
		.private_value = 2,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.info = usb6fire_control_output_vol_info,
		.get = usb6fire_control_output_vol_get,
		.put = usb6fire_control_output_vol_put,
		.tlv = { .p = tlv_output }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog 5/6 Playback Volume",
		.index = 0,
		.private_value = 4,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.info = usb6fire_control_output_vol_info,
		.get = usb6fire_control_output_vol_get,
		.put = usb6fire_control_output_vol_put,
		.tlv = { .p = tlv_output }
	},
	{}
};

static struct snd_kcontrol_new analog_mute_elements[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog 1/2 Playback Switch",
		.index = 0,
		.private_value = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = snd_ctl_boolean_stereo_info,
		.get = usb6fire_control_output_mute_get,
		.put = usb6fire_control_output_mute_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog 3/4 Playback Switch",
		.index = 0,
		.private_value = 2,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = snd_ctl_boolean_stereo_info,
		.get = usb6fire_control_output_mute_get,
		.put = usb6fire_control_output_mute_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog 5/6 Playback Switch",
		.index = 0,
		.private_value = 4,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = snd_ctl_boolean_stereo_info,
		.get = usb6fire_control_output_mute_get,
		.put = usb6fire_control_output_mute_put
	},
	{}
};

static struct snd_kcontrol_new analog_elements[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Line/Phono Capture Route",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = usb6fire_control_line_phono_info,
		.get = usb6fire_control_line_phono_get,
		.put = usb6fire_control_line_phono_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog Capture Volume",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.info = usb6fire_control_input_vol_info,
		.get = usb6fire_control_input_vol_get,
		.put = usb6fire_control_input_vol_put,
		.tlv = { .p = tlv_input }
	},
	{}
};

static struct snd_kcontrol_new spdif_elements[] = {
	{}
};

static int usb6fire_control_add_elements(struct control_runtime *rt, struct snd_card *card, struct snd_kcontrol_new *elements)
{
	int i = 0;
	int ret;

	while (elements[i].name) {
		ret = snd_ctl_add(card, snd_ctl_new1(&elements[i], rt));
		if (ret < 0)
			return ret;
		i++;
	}
	return 0;
}

static int usb6fire_control_add_virtual(
	struct control_runtime *rt,
	struct snd_card *card,
	char *name,
	struct snd_kcontrol_new *elems)
{
	int ret;
	int i;
	struct snd_kcontrol *vmaster = snd_ctl_make_virtual_master(name, NULL);
	struct snd_kcontrol *control;

	if (!vmaster)
		return -ENOMEM;
	ret = snd_ctl_add(card, vmaster);
	if (ret < 0)
		return ret;

	i = 0;
	while (elems[i].name) {
		control = snd_ctl_new1(&elems[i], rt);
		if (!control)
			return -ENOMEM;
		ret = snd_ctl_add(card, control);
		if (ret < 0)
			return ret;
		ret = snd_ctl_add_follower(vmaster, control);
		if (ret < 0)
			return ret;
		i++;
	}
	return 0;
}

int usb6fire_control_init(struct sfire_chip *chip)
{
	int i;
	int ret;
	struct control_runtime *rt = kzalloc(sizeof(struct control_runtime),
			GFP_KERNEL);
	struct comm_runtime *comm_rt = chip->comm;

	if (!rt)
		return -ENOMEM;

	rt->chip = chip;
	rt->update_streaming = usb6fire_control_streaming_update;
	rt->set_rate = usb6fire_control_set_rate;

	/* Set default volume to -10dB (160/180) and unmute all channels */
	for (i = 0; i < 6; i++)
		rt->output_vol[i] = 160;
	rt->output_mute = 0x3F; /* Unmute all 6 channels (bits 0-5) */

	i = 0;
	while (init_data[i].type) {
		comm_rt->write8(comm_rt, init_data[i].type, init_data[i].reg,
				init_data[i].value);
		i++;
	}

	usb6fire_control_opt_coax_update(rt);
	usb6fire_control_line_phono_update(rt);
	usb6fire_control_output_vol_update(rt);
	usb6fire_control_output_mute_update(rt);
	usb6fire_control_input_vol_update(rt);
	usb6fire_control_streaming_update(rt);

	if (PCM_MODE_USES_ANALOG(chip->pcm_mode)) {
		ret = usb6fire_control_add_virtual(rt, chip->card,
			"Master Playback Volume", analog_vol_elements);
		if (ret < 0) {
			printk(KERN_ERR "Cannot add control.");
			kfree(rt);
			return ret;
		}
		ret = usb6fire_control_add_virtual(rt, chip->card,
			"Master Playback Switch", analog_mute_elements);
		if (ret < 0) {
			printk(KERN_ERR "Cannot add control.");
			kfree(rt);
			return ret;
		}
		ret = usb6fire_control_add_elements(rt, chip->card, analog_elements);
		if (ret < 0) {
			printk(KERN_ERR "Cannot add control.");
			kfree(rt);
			return ret;
		}
	}
	
	if (PCM_MODE_USES_SPDIF(chip->pcm_mode)) {
		ret = usb6fire_control_add_elements(rt, chip->card, spdif_elements);
		if (ret < 0) {
			printk(KERN_ERR "Cannot add control.");
			kfree(rt);
			return ret;
		}
	}
	
	ret = usb6fire_control_add_elements(rt, chip->card, common_elements);
	if (ret < 0) {
		printk(KERN_ERR "Cannot add control.");
		kfree(rt);
		return ret;
	}

	chip->control = rt;
	return 0;
}

void usb6fire_control_abort(struct sfire_chip *chip)
{}

void usb6fire_control_destroy(struct sfire_chip *chip)
{
	kfree(chip->control);
	chip->control = NULL;
}
