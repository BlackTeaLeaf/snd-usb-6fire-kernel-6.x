/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Main routines and module definitions.
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

#include "chip.h"
#include "firmware.h"
#include "control.h"
#include "comm.h"
#include "midi.h"
#include "substream.h"
#include "urbs.h"
#include "pcm.h"

#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <sound/initval.h>

MODULE_AUTHOR("Torsten Schenk <torsten.schenk@zoho.com>");
MODULE_DESCRIPTION("TerraTec DMX 6Fire USB audio driver, version 0.6.1");
MODULE_LICENSE("GPL v2");

static int chip_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX; /* Index 0-max */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; /* Id for card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP; /* Enable card */
static struct sfire_chip *chips[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;
static struct usb_device *devices[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

static int tasklet_thresh[SNDRV_CARDS] = { PCM_TASKLET_THRESH_DEFAULT };
static int pcm_mode[SNDRV_CARDS] = { PCM_MODE_DEFAULT };

module_param_array_named(index, chip_index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the 6fire sound device");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the 6fire sound device.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable the 6fire sound device.");
module_param_array(tasklet_thresh, int, NULL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(tasklet_thresh, "Tasklet threshold (packets/urb, default = 128).");
module_param_array(pcm_mode, int, NULL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(pcm_mode, "PCM mode (default = 3).");

static DEFINE_MUTEX(register_mutex);

static void usb6fire_chip_abort(struct sfire_chip *chip)
{
	if (chip) {
		if (chip->card)
			snd_card_disconnect(chip->card);
		if (chip->midi)
			usb6fire_midi_abort(chip);
		if (chip->comm)
			usb6fire_comm_abort(chip);
		if (chip->control)
			usb6fire_control_abort(chip);
		if (chip->pcm)
			usb6fire_pcm_abort(chip);
		if (chip->urbs)
			usb6fire_urbs_abort(chip);
		if (chip->substream)
			usb6fire_substream_abort(chip);
		if (chip->card) {
			snd_card_free_when_closed(chip->card);
			chip->card = NULL;
		}
	}
}

static void usb6fire_chip_destroy(struct sfire_chip *chip)
{
	if (chip) {
		if (chip->midi)
			usb6fire_midi_destroy(chip);
		if (chip->comm)
			usb6fire_comm_destroy(chip);
		if (chip->control)
			usb6fire_control_destroy(chip);
		if (chip->pcm)
			usb6fire_pcm_destroy(chip);
		if (chip->urbs)
			usb6fire_urbs_destroy(chip);
		if (chip->substream)
			usb6fire_substream_destroy(chip);
		if (chip->card)
			snd_card_free(chip->card);
	}
}

static int usb6fire_chip_probe(struct usb_interface *intf,
		const struct usb_device_id *usb_id)
{
	int ret;
	int i;
	struct sfire_chip *chip = NULL;
	struct usb_device *device = interface_to_usbdev(intf);
	int regidx = -1; /* index in module parameter array */
	struct snd_card *card = NULL;

	/* check, if firmware is present on device, upload it if not */
	ret = usb6fire_fw_init(intf);
	if (ret < 0)
		return ret;
	else if (ret == FW_NOT_READY) /* firmware update performed */
		return 0;

	/* look if we already serve this card and return if so */
	mutex_lock(&register_mutex);
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (devices[i] == device) {
			if (chips[i])
				chips[i]->intf_count++;
			usb_set_intfdata(intf, chips[i]);
			mutex_unlock(&register_mutex);
			return 0;
		} else if (regidx < 0)
			regidx = i;
	}
	if (regidx < 0) {
		mutex_unlock(&register_mutex);
		printk(KERN_ERR PREFIX "too many cards registered.\n");
		return -ENODEV;
	}
	devices[regidx] = device;
	mutex_unlock(&register_mutex);

	/* if we are here, card can be registered in alsa. */
	if (usb_set_interface(device, 0, 0) != 0) {
		printk(KERN_ERR PREFIX "can't set first interface.\n");
		return -EIO;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	ret = snd_card_new(&intf->dev, chip_index[regidx], id[regidx],
			THIS_MODULE, sizeof(struct sfire_chip), &card);
#else
	ret = snd_card_create(chip_index[regidx], id[regidx],
			THIS_MODULE, sizeof(struct sfire_chip), &card);
#endif
	if (ret < 0) {
		printk(KERN_ERR PREFIX "cannot create alsa card.\n");
		return ret;
	}
	strcpy(card->driver, "6FireUSB");
	strcpy(card->shortname, "TerraTec DMX6FireUSB");
	sprintf(card->longname, "%s at %d:%d", card->shortname,
			device->bus->busnum, device->devnum);
	snd_card_set_dev(card, &intf->dev);

	chip = card->private_data;
	chips[regidx] = chip;
	chip->tasklet_thresh = tasklet_thresh[regidx];
	if (chip->tasklet_thresh < 0)
		chip->tasklet_thresh = PCM_TASKLET_THRESH_DEFAULT;
	chip->pcm_mode = pcm_mode[regidx];
	if (chip->pcm_mode < 0 || chip->pcm_mode >= PCM_MODES)
		chip->pcm_mode = PCM_MODE_DEFAULT;
	chip->dev = device;
	chip->regidx = regidx;
	chip->intf_count = 1;
	chip->card = card;

	ret = usb6fire_comm_init(chip);
	if (ret < 0) {
		usb6fire_chip_destroy(chip);
		return ret;
	}

	ret = usb6fire_midi_init(chip);
	if (ret < 0) {
		usb6fire_chip_destroy(chip);
		return ret;
	}

	ret = usb6fire_control_init(chip);
	if (ret < 0) {
		usb6fire_chip_destroy(chip);
		return ret;
	}
	
	if (chip->pcm_mode != PCM_MODE_OFF) {
		ret = usb6fire_substream_init(chip);
		if (ret < 0) {
			usb6fire_chip_destroy(chip);
			return ret;
		}
	
		ret = usb6fire_urbs_init(chip);
		if (ret < 0) {
			usb6fire_chip_destroy(chip);
			return ret;
		}
	
		ret = usb6fire_pcm_init(chip);
		if (ret < 0) {
			usb6fire_chip_destroy(chip);
			return ret;
		}
	}

	ret = snd_card_register(card);
	if (ret < 0) {
		printk(KERN_ERR PREFIX "cannot register card.");
		usb6fire_chip_destroy(chip);
		return ret;
	}
	usb_set_intfdata(intf, chip);
	return 0;
}

static void usb6fire_chip_disconnect(struct usb_interface *intf)
{
	struct sfire_chip *chip;
	struct snd_card *card;

	chip = usb_get_intfdata(intf);
	if (chip) { /* if !chip, fw upload has been performed */
		card = chip->card;
		chip->intf_count--;
		if (!chip->intf_count) {
			mutex_lock(&register_mutex);
			devices[chip->regidx] = NULL;
			chips[chip->regidx] = NULL;
			mutex_unlock(&register_mutex);

			chip->shutdown = true;
			usb6fire_chip_abort(chip);
			usb6fire_chip_destroy(chip);
		}
	}
}

static struct usb_device_id device_table[] = {
	{
		.match_flags = USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor = 0x0ccd,
		.idProduct = 0x0080
	},
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

static struct usb_driver usb_driver = {
	.name = "snd-usb-6fire",
	.probe = usb6fire_chip_probe,
	.disconnect = usb6fire_chip_disconnect,
	.id_table = device_table,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
module_usb_driver(usb_driver);
#else
static int __init usb6fire_chip_init(void)
{
	return usb_register(&usb_driver);
}

static void __exit usb6fire_chip_cleanup(void)
{
	usb_deregister(&usb_driver);
}

module_init(usb6fire_chip_init);
module_exit(usb6fire_chip_cleanup);
#endif
