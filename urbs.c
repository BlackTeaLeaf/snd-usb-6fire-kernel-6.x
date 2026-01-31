/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * URBS manager: manages usb stream
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

#include "urbs.h"
#include "chip.h"
#include "control.h"
#include "rates.h"

static struct urbs_runtime *runtimes[SNDRV_CARDS];
static DEFINE_SPINLOCK(runtimes_lock);

enum {
	STREAM_DISABLED, /* writer: pcm calls (start(), stop())*/
	STREAM_STOPPING, /* writer: tasklet */
	STREAM_STOPPED, /* writer: pcm calls (stop()) */
	STREAM_STARTING, /* writer: out urb retire */
	STREAM_STARTED, /* writer: pcm calls (start()) */
	STREAM_ENABLED /* writer: pcm calls (start(), stop())*/
};

enum {
	OUT_EP = 6,
	IN_EP = 2,
	ISOPACKET_HEADER_SIZE = 4,
	MAX_PACKETS_PER_URB = 512
};

static void usb6fire_urbs_enqueue(struct urbs_queue *queue, struct urb *urb)
{
	int node = atomic_read(&queue->tail);

	queue->node[node] = urb;
	node++;
	node %= URBS_COUNT + 1;
	atomic_set(&queue->tail, node);
}

static struct urb *usb6fire_urbs_dequeue(struct urbs_queue *queue)
{
	int node = atomic_read(&queue->head);
	struct urb *urb = NULL;

	if(node != atomic_read(&queue->tail)) {
		urb = queue->node[node];
		node++;
		node %= URBS_COUNT + 1;
		atomic_set(&queue->head, node);
	}
	return urb;
}

static void usb6fire_urbs_clearqueue(struct urbs_queue *queue)
{
	atomic_set(&queue->head, 0);
	atomic_set(&queue->tail, 0);
}

static void usb6fire_urbs_init_out_header(
	struct usb_iso_packet_descriptor *packets, int p, u8 *buffer,
	int preframe_count, int frame_count, int frame_size)
{
	struct usb_iso_packet_descriptor *packet = packets + p;

	packet->offset = p * ISOPACKET_HEADER_SIZE + preframe_count * frame_size;
	packet->length = ISOPACKET_HEADER_SIZE + frame_count * frame_size;
	packet->status = 0;
	packet->actual_length = 0;
	buffer += packet->offset;
	buffer[0] = 0xaa;
	buffer[1] = 0xaa;
	buffer[2] = frame_count;
	buffer[3] = 0x00;
}

static void usb6fire_urbs_process_sub(struct urbs_substream *sub, struct urb *urb)
{
	int p;
	int c;
	u8 *buffer;
	int frame_count;	
	
	for (p = 0; p < urb->number_of_packets; p++) {
		buffer = urb->transfer_buffer + urb->iso_frame_desc[p].offset;
		frame_count = buffer[2];
		buffer += ISOPACKET_HEADER_SIZE;
		c = 0;
		if (sub->active && sub->state == URBS_SUBSTATE_ENABLED)
			for (; c < sub->channels; c++)
				sub->copier[c].exec(sub->copier + c, buffer, frame_count);
		for (; c < sub->max_channels; c++)
			sub->muter[c].exec(sub->muter + c, buffer, frame_count);
	}
}

static void usb6fire_urbs_process(struct urbs_runtime *rt, struct urb *in_urb)
{
	int frame_count;
	int p;
	int s;
	int state;
	u8 *si;
	struct urbs_substream *sub;
	struct urb *out_urb;
	unsigned long flags;
	int total_frame_count = 0;

	state = atomic_read(&rt->state);
	if (state == STREAM_DISABLED || state == STREAM_STOPPING)
		return;

	for (s = 0; s < rt->n_substreams; s++) {
		sub = rt->substreams + s;
		if (sub->active != sub->trigger_active) {
			sub->active = sub->trigger_active;
			sub->resetter.exec(&sub->resetter, rt->rate);
		}
	}

	if (in_urb->status)
		return;
	else {
		for (p = 0; p < in_urb->number_of_packets; p++)
			if(in_urb->iso_frame_desc[p].status)
				break;
		if (p != in_urb->number_of_packets)
			return;
	}

	out_urb = usb6fire_urbs_dequeue(&rt->out_queue);

	if (out_urb) {
		if (out_urb->number_of_packets != in_urb->number_of_packets)
			printk(KERN_WARNING PREFIX "Packet count of out/in urbs differ: "
				"o = %d, i = %d", out_urb->number_of_packets,
				in_urb->number_of_packets);
		else if (state == STREAM_ENABLED) {
			for (p = 0; p < in_urb->number_of_packets; p++) {
				if (in_urb->iso_frame_desc[p].actual_length > ISOPACKET_HEADER_SIZE)
					frame_count = (in_urb->iso_frame_desc[p].actual_length - ISOPACKET_HEADER_SIZE) / rt->in_frame_size;
				else
					frame_count = 0;
				si = in_urb->transfer_buffer + in_urb->iso_frame_desc[p].offset + 2;
				*si = frame_count;
				
				usb6fire_urbs_init_out_header(out_urb->iso_frame_desc, p, out_urb->transfer_buffer, total_frame_count, frame_count, rt->out_frame_size);
				total_frame_count += frame_count;
			}

			for (s = 0; s < rt->n_substreams; s++) {
				sub = rt->substreams + s;
				spin_lock_irqsave(&sub->lock, flags);
				if (sub->in)
					usb6fire_urbs_process_sub(sub, in_urb);
				else
					usb6fire_urbs_process_sub(sub, out_urb);
				if (sub->active && sub->state == URBS_SUBSTATE_ENABLED) {
					sub->dma_off += total_frame_count;
					sub->dma_off %= sub->dma_size;
					atomic_set(&sub->dma_off_public, sub->dma_off);
					sub->period_off += total_frame_count;
					if (sub->period_off >= sub->period_size) {
						sub->period_off %= sub->period_size;
						spin_unlock_irqrestore(&sub->lock, flags);
						snd_pcm_period_elapsed(sub->alsa_sub);
					}
					else
						spin_unlock_irqrestore(&sub->lock, flags);
				}
				else
					spin_unlock_irqrestore(&sub->lock, flags);
			}
		}
		else if (state == STREAM_STARTING) {
			for (p = 0; p < in_urb->number_of_packets; p++) {
				si = in_urb->transfer_buffer + in_urb->iso_frame_desc[p].offset;
				usb6fire_urbs_init_out_header(out_urb->iso_frame_desc, p, out_urb->transfer_buffer, 0, 0, 0);
			}
		}
		else if (state == STREAM_STARTED) {
			for (p = 0; p < in_urb->number_of_packets; p++) {
				si = in_urb->transfer_buffer + in_urb->iso_frame_desc[p].offset;
				usb6fire_urbs_init_out_header(out_urb->iso_frame_desc, p, out_urb->transfer_buffer, 0, 0, 0);
			}
			wake_up(&rt->stream_wait_queue);
		}
	}
	
	if (out_urb)
		if (usb_submit_urb(out_urb, GFP_ATOMIC) < 0)
			printk(KERN_ERR PREFIX "urb submission failed (out)\n");
	if (usb_submit_urb(in_urb, GFP_ATOMIC) < 0)
		printk(KERN_ERR PREFIX "urb submission failed (in)\n");
}

static void usb6fire_urbs_direct_in_retire(struct urb *urb)
{
	struct urbs_runtime *rt = urb->context;

	usb6fire_urbs_process(rt, urb);
}

static void usb6fire_urbs_direct_out_retire(struct urb *urb)
{
	struct urbs_runtime *rt = urb->context;

	if (atomic_read(&rt->state) == STREAM_STARTING)
		atomic_set(&rt->state, STREAM_STARTED);
	usb6fire_urbs_enqueue(&rt->out_queue, urb);
}

static void usb6fire_urbs_tasklet_in_retire(struct urb *urb)
{
	struct urbs_runtime *rt = urb->context;

	usb6fire_urbs_enqueue(&rt->in_queue, urb);
	tasklet_schedule(&rt->tasklet);
}

static void usb6fire_urbs_tasklet_out_retire(struct urb *urb)
{
	struct urbs_runtime *rt = urb->context;

	if (atomic_read(&rt->state) == STREAM_STARTING)
		atomic_set(&rt->state, STREAM_STARTED);
	usb6fire_urbs_enqueue(&rt->out_queue, urb);
	tasklet_schedule(&rt->tasklet);
}

static void usb6fire_urbs_tasklet(unsigned long regidx)
{
	struct urbs_runtime *rt;
	struct urb *urb;

	spin_lock(&runtimes_lock);
	rt = runtimes[regidx];
	spin_unlock(&runtimes_lock);
	if (!rt)
		return;

	while ((urb = usb6fire_urbs_dequeue(&rt->in_queue)))
		usb6fire_urbs_process(rt, urb);
}

static int usb6fire_urbs_set_rate(struct urbs_runtime *rt, unsigned int rate_id)
{
	int ret;
	struct control_runtime *ctrl_rt = rt->chip->control;

	ctrl_rt->usb_streaming = false;
	ret = ctrl_rt->update_streaming(ctrl_rt);
	if (ret < 0) {
		printk(KERN_ERR PREFIX "error stopping streaming while "
			"setting samplerate %d.\n", rates[rate_id]);
		return ret;
	}

	ret = ctrl_rt->set_rate(ctrl_rt, rates[rate_id]);
	if (ret < 0) {
		printk(KERN_ERR PREFIX "error setting samplerate %d.\n", rates[rate_id]);
		return ret;
	}

	ctrl_rt->usb_streaming = true;
	ret = ctrl_rt->update_streaming(ctrl_rt);
	if (ret < 0) {
		printk(KERN_ERR PREFIX "error starting streaming while "
			"setting samplerate %d.\n", rates[rate_id]);
		return ret;
	}
	return 0;
}

static void usb6fire_urbs_stop(struct urbs_runtime *rt)
{
	int u;
	
	atomic_set(&rt->state, STREAM_STOPPING);
	for (u = 0; u < URBS_COUNT; u++) {
		usb_kill_urb(rt->in_urbs[u]);
		usb_kill_urb(rt->out_urbs[u]);
	}
	if (rt->tasklet_enabled) {
		tasklet_disable(&rt->tasklet);
		rt->tasklet_enabled = false;
	}
	atomic_set(&rt->state, STREAM_DISABLED);
	rt->packets_per_urb = 0;
	rt->rate = 0;
}

static int usb6fire_urbs_modify(struct urb *urb, unsigned int packet_size,
	unsigned int packet_count, bool use_tasklet, bool in)
{
	unsigned int size = packet_size * packet_count;

	if (use_tasklet)
		if (in)
			urb->complete = usb6fire_urbs_tasklet_in_retire;
		else
			urb->complete = usb6fire_urbs_tasklet_out_retire;
	else if (in)
			urb->complete = usb6fire_urbs_direct_in_retire;
		else
			urb->complete = usb6fire_urbs_direct_out_retire;

	if (urb->transfer_buffer && urb->transfer_buffer_length < size) {
		kfree(urb->transfer_buffer);
		urb->transfer_buffer = NULL;
		urb->transfer_buffer_length = 0;
	}
	if (!urb->transfer_buffer) {
		urb->transfer_buffer = kmalloc(size, GFP_KERNEL);
		if (!urb->transfer_buffer) {
			printk(KERN_ERR PREFIX "Error allocating urb buffer of size %d:"
				"not enough memory.", size);
			return -ENOMEM;
		}
		urb->transfer_buffer_length = size;
	}
	urb->number_of_packets = packet_count;
	return 0;
}

static int usb6fire_urbs_start(struct urbs_runtime *rt, unsigned int rate_id,
	unsigned int period_size)
{
	int u;
	int p;
	struct usb_iso_packet_descriptor *packet;
	int ret;
	unsigned int packets_per_urb;

	if (rate_id >= ARRAY_SIZE(rates)) {
		printk(KERN_ERR PREFIX "Invalid rate selected. ID: %d.\n", rate_id);
		return -EINVAL;
	}
	atomic_set(&rt->state, STREAM_STARTING);
	usb6fire_urbs_clearqueue(&rt->in_queue);
	usb6fire_urbs_clearqueue(&rt->out_queue);
	packets_per_urb = period_size / rates_max_fpp[rate_id];
	if (!packets_per_urb) {
		printk(KERN_ERR PREFIX "Invalid period size %d selected for rate %d.\n",
			period_size, rates[rate_id]);
		return -EINVAL;
	}
	if (packets_per_urb > MAX_PACKETS_PER_URB)
		packets_per_urb = MAX_PACKETS_PER_URB;
	rt->use_tasklet = rt->chip->tasklet_thresh
		&& packets_per_urb >= rt->chip->tasklet_thresh;
	for (u = 0; u < URBS_COUNT; u++) {
		ret = usb6fire_urbs_modify(rt->out_urbs[u],
			rates_out_packet_size[rate_id], packets_per_urb, rt->use_tasklet, false);
		if (ret)
			return ret;
		ret = usb6fire_urbs_modify(rt->in_urbs[u],
			rates_in_packet_size[rate_id], packets_per_urb, rt->use_tasklet, true);
		if (ret)
			return ret;
	}

	if (rt->use_tasklet && !rt->tasklet_enabled) {
		tasklet_enable(&rt->tasklet);
		rt->tasklet_enabled = true;
	}
	for (u = 0; u < URBS_COUNT; u++)
		usb6fire_urbs_enqueue(&rt->out_queue, rt->out_urbs[u]);
	for (u = 0; u < URBS_COUNT; u++) {
		for (p = 0; p < packets_per_urb; p++) {
			packet = rt->in_urbs[u]->iso_frame_desc + p;
			packet->offset = p * rates_in_packet_size[rate_id];
			packet->length = rates_in_packet_size[rate_id];
			packet->actual_length = 0;
			packet->status = 0;
		}
		ret = usb_submit_urb(rt->in_urbs[u],
				GFP_ATOMIC);
		if (ret) {
			printk(KERN_ERR PREFIX "Error %d while starting stream.\n", ret);
			atomic_set(&rt->state, STREAM_DISABLED);
			for (u = 0; u < URBS_COUNT; u++) {
				usb_kill_urb(rt->in_urbs[u]);
				usb_kill_urb(rt->out_urbs[u]);
			}
			return ret;
		}
	}
	wait_event_timeout(rt->stream_wait_queue,
		atomic_read(&rt->state) == STREAM_STARTED,
		msecs_to_jiffies(1000));
	if (atomic_read(&rt->state) != STREAM_STARTED) {
		printk(KERN_ERR PREFIX "Error while starting stream: timeout.\n");
		usb6fire_urbs_stop(rt);
		return -EIO;
	}
	else {
		rt->packets_per_urb = packets_per_urb;
		rt->rate = rates[rate_id];
		atomic_set(&rt->state, STREAM_ENABLED);
		return 0;
	}
}

static int usb6fire_urbs_get_min_psize(struct urbs_runtime *rt, unsigned int rate)
{
	int i;

	if (rt->packets_per_urb) {
		for (i = 0; i < ARRAY_SIZE(rates); i++)
			if (rates[i] == rt->rate)
				return rt->packets_per_urb * rates_max_fpp[i];
	} else
		for (i = 0; i < ARRAY_SIZE(rates); i++)
			if (rates[i] == rate)
				return rates_max_fpp[i];
	return 0;
}

static int usb6fire_urbs_set_state(struct urbs_runtime *rt, struct substream_descriptor *desc, int state)
{
	struct urbs_substream *sub = rt->substreams + desc->index;
	struct substream_runtime *sub_rt = rt->chip->substream;
	int ret;
	int i;
	unsigned long flags;
	unsigned int rate_id;
	
	switch(state)
	{
	case URBS_SUBSTATE_ENABLED:
		for (rate_id = 0; rate_id < ARRAY_SIZE(rates); rate_id++)
			if (rates[rate_id] == desc->alsa_sub->runtime->rate)
				break;
		if (rate_id == ARRAY_SIZE(rates))
			return -EINVAL;
		if (rt->substreams[desc->index].state == URBS_SUBSTATE_DISABLED) {
			if (atomic_read(&rt->state) == STREAM_DISABLED) {
				ret = usb6fire_urbs_set_rate(rt, rate_id);
				if (ret < 0) {
					printk(KERN_ERR PREFIX "error setting rate %d.\n",
						desc->alsa_sub->runtime->rate);
					return ret;
				}
				ret = usb6fire_urbs_start(rt, rate_id,
					desc->alsa_sub->runtime->period_size);
				if (ret < 0) {
					printk(KERN_ERR PREFIX "error starting usb stream.\n");
					return ret;
				}
			} else if (rt->rate != desc->alsa_sub->runtime->rate) {
				printk(KERN_ERR PREFIX "error setting rate: device busy.\n");
				return -EINVAL;
			}
		}
		
		spin_lock_irqsave(&sub->lock, flags);
		sub->state = URBS_SUBSTATE_ENABLED;
		sub->channels = desc->alsa_sub->runtime->channels;
		sub->active = false;
		sub->trigger_active = false;
		sub->dma_off = 0;
		sub->period_off = 0;
		sub->dma_size = desc->alsa_sub->runtime->buffer_size;
		sub->period_size = desc->alsa_sub->runtime->period_size;
		atomic_set(&sub->dma_off_public, 0);
		sub->alsa_sub = desc->alsa_sub;
		for (i = 0; i < sub->channels; i++)
			sub->copier[i] = sub->get_copier(sub_rt, desc, i);
		spin_unlock_irqrestore(&sub->lock, flags);
		break;
		
	case URBS_SUBSTATE_DISABLED:
		if (rt->substreams[desc->index].state != URBS_SUBSTATE_DISABLED) {
			spin_lock_irqsave(&sub->lock, flags);
			sub->channels = 0;
			sub->state = URBS_SUBSTATE_DISABLED;
			sub->active = false;
			sub->trigger_active = false;
			sub->alsa_sub = NULL;
			spin_unlock_irqrestore(&sub->lock, flags);
			
			for (i = 0; i < rt->n_substreams; i++)
				if (rt->substreams[i].state != URBS_SUBSTATE_DISABLED)
					break;
			if (i == rt->n_substreams)
				usb6fire_urbs_stop(rt);
		}
		break;
	}

	return 0;
}

static int usb6fire_urbs_set_active(struct urbs_runtime *rt, struct substream_descriptor *desc, bool active)
{
	struct urbs_substream *sub = rt->substreams + desc->index;

	sub->trigger_active = active;
	return 0;
}

static snd_pcm_uframes_t usb6fire_urbs_get_pointer(struct urbs_runtime *rt,
	struct substream_descriptor *desc)
{
	struct urbs_substream *sub = rt->substreams + desc->index;

	return atomic_read(&sub->dma_off_public);
}

static struct urb *usb6fire_urbs_create(struct urbs_runtime *rt,
	bool in, int ep)
{
	struct urb *urb = usb_alloc_urb(MAX_PACKETS_PER_URB, GFP_KERNEL);
	urb->dev = rt->chip->dev;
	urb->pipe = in ? usb_rcvisocpipe(urb->dev, ep)
		: usb_sndisocpipe(urb->dev, ep);
	urb->interval = 1;
	urb->transfer_flags = URB_ISO_ASAP;
	urb->context = rt;

	return urb;
}

int usb6fire_urbs_init(struct sfire_chip *chip)
{
	int i;
	int k;
	struct substream_runtime *sub_rt = chip->substream;
	struct substream_definition sub_def;
	struct urbs_runtime *rt =
		kzalloc(sizeof(struct urbs_runtime), GFP_KERNEL);
	unsigned long flags;
	
	if(!rt)
		return -ENOMEM;

	spin_lock_irqsave(&runtimes_lock, flags);
	runtimes[chip->regidx] = rt;
	spin_unlock_irqrestore(&runtimes_lock, flags);

	rt->chip = chip;
	init_waitqueue_head(&rt->stream_wait_queue);
	atomic_set(&rt->state, STREAM_DISABLED);
	
	rt->n_substreams = sub_rt->n_substreams;
	for (i = 0; i < rt->n_substreams; i++) {
		rt->substreams[i].state = URBS_SUBSTATE_DISABLED;
		spin_lock_init(&rt->substreams[i].lock);
		sub_def = sub_rt->get_definition(sub_rt, i);
		rt->substreams[i].in = sub_def.in;
		rt->substreams[i].max_channels = sub_def.max_channels;
		rt->substreams[i].copier = kzalloc(sub_def.max_channels * sizeof(struct substream_copier), GFP_KERNEL);
		rt->substreams[i].muter = kzalloc(sub_def.max_channels * sizeof(struct substream_muter), GFP_KERNEL);
		rt->substreams[i].get_copier = sub_def.get_copier;
		rt->substreams[i].resetter = sub_def.get_resetter(sub_rt);
		for (k = 0; k < sub_def.max_channels; k++)
			rt->substreams[i].muter[k] = sub_def.get_muter(sub_rt, k);
	}
	
	for(i = 0; i < URBS_COUNT; i++) {
		rt->in_urbs[i] = usb6fire_urbs_create(rt, true, IN_EP);
		rt->out_urbs[i] = usb6fire_urbs_create(rt, false, OUT_EP);
	}

	if (chip->tasklet_thresh) {
		tasklet_init(&rt->tasklet, usb6fire_urbs_tasklet, rt->chip->regidx);
		rt->tasklet_enabled = true;
	}
		
	rt->in_frame_size = sub_rt->in_urb_frame_size;
	rt->out_frame_size = sub_rt->out_urb_frame_size;
	rt->set_state = usb6fire_urbs_set_state;
	rt->set_active = usb6fire_urbs_set_active;
	rt->get_pointer = usb6fire_urbs_get_pointer;
	rt->get_min_period_size = usb6fire_urbs_get_min_psize;
	
	chip->urbs = rt;
	
	return 0;
}

void usb6fire_urbs_abort(struct sfire_chip *chip)
{
	struct urbs_runtime *rt = chip->urbs;
	unsigned long flags;
	int i;
	
	spin_lock_irqsave(&runtimes_lock, flags);
	runtimes[chip->regidx] = NULL;
	spin_unlock_irqrestore(&runtimes_lock, flags);

	if (rt) {
		for (i = 0; i < rt->n_substreams; i++)
			if (rt->substreams[i].alsa_sub) {
				rt->substreams[i].active = false;
				snd_pcm_stop(rt->substreams[i].alsa_sub,
					SNDRV_PCM_STATE_DISCONNECTED);
			}
		usb6fire_urbs_stop(rt);

		if (rt->chip->tasklet_thresh) {
			if (!rt->tasklet_enabled) {
				tasklet_enable(&rt->tasklet);
				rt->tasklet_enabled = true;
			}
			tasklet_kill(&rt->tasklet);
		}
	}
}

void usb6fire_urbs_destroy(struct sfire_chip *chip)
{
	int i;
	struct urbs_runtime *rt = chip->urbs;

	if (rt) {
		for (i = 0; i < rt->n_substreams; i++) {
			kfree(rt->substreams[i].copier);
			kfree(rt->substreams[i].muter);
			rt->substreams[i].copier = NULL;
			rt->substreams[i].muter = NULL;
		}
		for (i = 0; i < URBS_COUNT; i++) {
			if (rt->in_urbs[i]->transfer_buffer)
				kfree(rt->in_urbs[i]->transfer_buffer);
			if (rt->out_urbs[i]->transfer_buffer)
				kfree(rt->out_urbs[i]->transfer_buffer);
			rt->in_urbs[i]->transfer_buffer = NULL;
			rt->out_urbs[i]->transfer_buffer = NULL;
			usb_free_urb(rt->in_urbs[i]);
			usb_free_urb(rt->out_urbs[i]);
			rt->in_urbs[i] = NULL;
			rt->out_urbs[i] = NULL;
		}
		kfree(rt);
	}
	chip->urbs = NULL;
}
