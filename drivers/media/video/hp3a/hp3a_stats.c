/*
 * drivers/media/video/hp3a/hp3a_stats.c
 *
 * HP Imaging/3A Driver : 3A framework management.
 *
 * Copyright (C) 2008-2009 Hewlett-Packard Co.
 *
 * Contributors:
 *		Tanvir Islam <tanvir.islam@hp.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "hp3a_common.h"
#include "hp3a_queue.h"
#include "../oldomap34xxcam.h"
#include "ispccdc.h"

struct hp3a_context g_tc;
static void hp3a_task(struct work_struct *);
DECLARE_WORK(g_hp3a_work_queue, hp3a_task);

/**
 * initialize_hp3a_framework - Initializes hp3a framework.
 * @device: Pointer to a hp3a device structure.
 *
 * No return value.
 **/
void initialize_hp3a_framework(struct hp3a_dev *device)
{
	if (g_tc.initialized == 0) {
		spin_lock_init(&g_tc.stats_lock);
		spin_lock_init(&g_tc.hist_lock);
		spin_lock_init(&g_tc.af_lock);
		spin_lock_init(&g_tc.hardpipe_lock);
		init_completion(&g_tc.frame_done);
		g_tc.frame_done.done = 0;
		init_waitqueue_head(&g_tc.stats_done);

		/* Initialize default values for task related data members. */
		g_tc.v4l2_streaming = 0;
		g_tc.update_hardpipe = 0;
		g_tc.hist_hw_configured = 0;
		g_tc.hist_bin_size = 0;
		g_tc.af_hw_configured = 0;
		g_tc.raw_hw_configured = 0;
		g_tc.raw_frequency = MIN_RAW_CAPTURE_INTERVAL;
		g_tc.raw_cap_sched_count = 0;
		g_tc.req_af_buffer_size = -1;
		g_tc.req_raw_buffer_size = -1;
		g_tc.raw_width = 0;
		g_tc.raw_height = 0;
		g_tc.histogram_buffer = NULL;
		g_tc.af_buffer  = NULL;
		g_tc.raw_buffer  = NULL;
		g_tc.exposure_sync = 2;
		g_tc.gain_sync = 1;

		/* Initialize task queues. */
		hp3a_initialize_queue(&g_tc.hist_stat_queue, 8,
			sizeof(struct hp3a_internal_buffer *));
		hp3a_initialize_queue(&g_tc.af_stat_queue, 8,
			sizeof(struct hp3a_internal_buffer *));
		hp3a_initialize_queue(&g_tc.raw_frame_queue, 4,
			sizeof(struct hp3a_internal_buffer *));
		hp3a_initialize_queue(&g_tc.sensor_write_queue, 4,
			sizeof(struct hp3a_sensor_param_internal));
		hp3a_initialize_queue(&g_tc.sensor_read_queue, 8,
			sizeof(struct hp3a_sensor_param_internal));
		hp3a_initialize_queue(&g_tc.hist_hw_queue, 4,
			sizeof(struct hp3a_internal_buffer *));
		hp3a_initialize_queue(&g_tc.ready_stats_queue, 8,
			sizeof(struct hp3a_internal_buffer *));

		g_tc.initialized = 1;
	}
}

/**
 * deinitialize_hp3a_framework - Deinitializes hp3a framework.
 *
 * No return value.
 **/
void deinitialize_hp3a_framework(void)
{
	if (g_tc.initialized == 1) {
		hp3a_deinitialize_queue(&g_tc.ready_stats_queue);
		hp3a_deinitialize_queue(&g_tc.hist_hw_queue);
		hp3a_deinitialize_queue(&g_tc.sensor_write_queue);
		hp3a_deinitialize_queue(&g_tc.sensor_read_queue);
		hp3a_deinitialize_queue(&g_tc.raw_frame_queue);
		hp3a_deinitialize_queue(&g_tc.af_stat_queue);
		hp3a_deinitialize_queue(&g_tc.hist_stat_queue);

		g_tc.initialized = 0;
	}
}

/**
 * hp3a_framework_start - Stops hp3a framework.
 *
 * No return value.
 **/
void hp3a_framework_start(struct hp3a_fh *fh)
{
	fh->buffer_count = 0;
	fh->buffers = NULL;
	g_tc.frame_done.done = 0;
	g_tc.current_exposure = 0;
	g_tc.current_gain = 0;
	g_tc.exposure = 0;
	g_tc.gain = 0;
	g_tc.hist_done = 0;
	g_tc.hist_hw_enable = 0;
	g_tc.af_hw_enable = 0;
	g_tc.raw_hw_configured = 0;
	g_tc.isp_ctx_saved = 0;
}

/**
 * hp3a_framework_stop - Stops hp3a framework.
 *
 * No return value.
 **/
void hp3a_framework_stop(struct hp3a_fh *fh)
{
	int i;
	unsigned long irqflags = 0;

	spin_lock_irqsave(&g_tc.stats_lock, irqflags);

	/* Reset flags. */
	g_tc.v4l2_streaming = 0;

	/* Need to flush queue. */
	hp3a_flush_queue(&g_tc.sensor_write_queue);
	hp3a_flush_queue(&g_tc.sensor_read_queue);
	hp3a_flush_queue(&g_tc.raw_frame_queue);
	hp3a_flush_queue(&g_tc.af_stat_queue);
	hp3a_flush_queue(&g_tc.hist_stat_queue);
	hp3a_flush_queue(&g_tc.hist_hw_queue);
	hp3a_flush_queue(&g_tc.ready_stats_queue);

	/* Internal buffer clean up. */
	for (i = 0; i < fh->buffer_count; ++i) {
		unmap_buffer_from_kernel(&(fh->buffers[i]));
	}
	kfree(fh->buffers);
	fh->buffers = NULL;

	/* Initialize configs to default. */
	g_tc.raw_frequency = MIN_RAW_CAPTURE_INTERVAL;
	g_tc.hist_hw_configured = 0;
	g_tc.af_hw_configured = 0;
	g_tc.raw_hw_configured = 0;
	g_tc.histogram_buffer = NULL;
	g_tc.af_buffer  = NULL;
	g_tc.raw_buffer  = NULL;

	spin_unlock_irqrestore(&g_tc.stats_lock, irqflags);

	/* Release any task waiting for stats. */
	complete(&g_tc.frame_done);
	g_tc.hist_done = 1;
	wake_up_all(&g_tc.stats_done);
}

/**
 * hp3a_set_sensor_param - Set sensor specific params.
 * @param: Pointer the structure containing sensor parameters.
 * @fh: Pointer to a hp3a_fh stucture.
 *
 * Return 0 on success,  less than 0 otherwise.
 **/
int hp3a_set_sensor_param(struct hp3a_sensor_param *param, struct hp3a_fh *fh)
{
	int ret = -1;
	struct hp3a_sensor_param_internal sensor_param;

	if (likely(fh->v4l2_dev > -1)) {
		if (likely(g_tc.v4l2_streaming == 1)) {
			sensor_param.v4l2_dev = fh->v4l2_dev;
			ret = 0;

			if (g_tc.current_exposure != param->exposure) {
				sensor_param.exposure = param->exposure;
				sensor_param.gain = 0;

				ret = hp3a_enqueue(&g_tc.sensor_write_queue,
							&sensor_param);
			}

			if (g_tc.current_gain != param->gain) {
				sensor_param.exposure = 0;
				sensor_param.gain = param->gain;

				ret = hp3a_enqueue(&g_tc.sensor_write_queue,
						&sensor_param);
			}
		} else {
			struct cam_sensor_settings sensor_settings = {
			.flags = 0,
			.exposure = 0,
			.gain = 0,
			.regs = 0,
			.reg_data = 0};

			sensor_settings.exposure = param->exposure;
			sensor_settings.gain = param->gain;
			sensor_settings.flags = (OMAP34XXCAM_SET_GAIN | \
						OMAP34XXCAM_SET_EXPOSURE);

			/**
			* Write and read sensor settings.
			*/
			ret = omap34xxcam_sensor_settings(fh->v4l2_dev,
							&sensor_settings);
		}
	} else {
		dev_err(fh->device->dev, "hp3a: Invalid sensor id(%d)\n", fh->v4l2_dev);
	}

	return ret;
}

/**
 * hp3a_set_hardpipe_param - Set hard pipe specific params to be programmed.
 * @param: Pointer the structure containing hard pipe parameters.
 * @fh: Pointer to a hp3a_fh stucture.
 *
 * Return 0 on success,  less than 0 otherwise.
 **/
int hp3a_set_hardpipe_param(struct hp3a_hardpipe_param *param,
				struct hp3a_fh *fh)
{
	unsigned long irqflags = 0;

	spin_lock_irqsave(&g_tc.hardpipe_lock, irqflags);
	memcpy(&g_tc.hpipe_param, param, sizeof(struct hp3a_hardpipe_param));
	g_tc.update_hardpipe = 1;
	spin_unlock_irqrestore(&g_tc.hardpipe_lock, irqflags);

	if (g_tc.v4l2_streaming == 0) {
		hp3a_update_hardpipe();
	}

	return 0;
}

/**
 * hp3a_collect_statsistics - Collect 3A statistics.
 * @work: Pointer to the  hp3a statistics structure.
 *
 * No return value.
 **/
int hp3a_collect_statistics(struct hp3a_statistics *stat)
{
	unsigned long irqflags = 0;

	if (unlikely(g_tc.v4l2_streaming == 0)) {
		return -1;
	}

	/* Initialize buffer indexes. */
	stat->hist_stat_index = -1;
	stat->af_stat_index = -1;
	stat->raw_frame_index = -1;
	stat->exposure = 0;
	stat->gain = 0;

	if (wait_for_completion_interruptible(&g_tc.frame_done) == 0 &&
						g_tc.v4l2_streaming == 1) {
		g_tc.frame_done.done = 0;

		/* Wait for all stats tasks to be done. */
		if (g_tc.hist_done == 0) {
			wait_event_interruptible_timeout(g_tc.stats_done,
				(g_tc.hist_done == 1), msecs_to_jiffies(8));
		}

		spin_lock_irqsave(&g_tc.stats_lock, irqflags);

		/* Frame meta data. */
		stat->frame_id = g_tc.frame_count;
		stat->exposure = g_tc.exposure;
		stat->gain = g_tc.gain;

		/* Raw bayer frame. */
		if (g_tc.raw_buffer != NULL) {
			stat->raw_frame_index = g_tc.raw_buffer->index;
			stat->raw_width = g_tc.raw_width;
			stat->raw_height = g_tc.raw_height;
			g_tc.raw_buffer = NULL;
		}

		/* AF Paxel. */
		if (g_tc.af_buffer != NULL) {
			stat->af_stat_index = g_tc.af_buffer->index;
			g_tc.af_buffer = NULL;
		}

		/* Histogram. */
		if (g_tc.histogram_buffer != NULL) {
			if (g_tc.hist_done  == 1) {
				stat->hist_stat_index = g_tc.histogram_buffer->index;
			} else {
				hp3a_enqueue(&g_tc.hist_stat_queue, &g_tc.histogram_buffer);
			}
			g_tc.histogram_buffer = NULL;
		}

		g_tc.hist_done = 0;

		spin_unlock_irqrestore(&g_tc.stats_lock, irqflags);
	}

	return 0;
}

/**
 * hp3a_update_framework- execute tasks between frames.
 *
 * No return value.
 **/
void hp3a_update_stats_readout_done(void)
{
	int i;
	struct hp3a_internal_buffer *ibuffer;
	struct hp3a_sensor_param_internal sensor_param;

	if (unlikely(g_tc.v4l2_streaming == 0)) {
		hp3a_disable_histogram();
		hp3a_disable_af();
		return;
	}

	/* Reuse stats buffers. */
	if (g_tc.histogram_buffer != NULL) {
		g_tc.hist_done = 0;
		hp3a_enqueue(&g_tc.hist_stat_queue,
			 &g_tc.histogram_buffer);
		g_tc.histogram_buffer = NULL;
	}

	if (g_tc.af_buffer  != NULL) {
		hp3a_enqueue(&g_tc.af_stat_queue,
			&g_tc.af_buffer);
		g_tc.af_buffer = NULL;
	}

	if (g_tc.raw_buffer != NULL) {
		hp3a_enqueue(&g_tc.raw_frame_queue,
				&g_tc.raw_buffer);
		g_tc.raw_buffer = NULL;
	}

	/* Process ready stats. */
	for (i = MAX_STAT_BUFFERS_PER_FRAME; i--;) {
		ibuffer = NULL;
		if (hp3a_dequeue(&g_tc.ready_stats_queue, &ibuffer) == 0) {
			if (ibuffer->type == HISTOGRAM && g_tc.histogram_buffer == NULL)
				g_tc.histogram_buffer = ibuffer;
			else if (ibuffer->type == PAXEL && g_tc.af_buffer == NULL)
				g_tc.af_buffer = ibuffer;
			else if (ibuffer->type == BAYER && g_tc.raw_buffer == NULL)
				g_tc.raw_buffer = ibuffer;
			else {
				printk(KERN_ERR "hp3a: Error unknown buffer type(%d) in"
							" ready queue\n", ibuffer->type);
			}
		} else {
			break;
		}
	}

	for (i = QUEUE_COUNT(g_tc.sensor_read_queue); i--;) {
		if (hp3a_dequeue(&g_tc.sensor_read_queue, &sensor_param) == 0) {
			if (sensor_param.frame_id == g_tc.frame_count) {
				if (sensor_param.exposure)
					g_tc.exposure = sensor_param.exposure;
				if (sensor_param.gain)
					g_tc.gain = sensor_param.gain;
			} else if (sensor_param.frame_id > g_tc.frame_count) {
				hp3a_enqueue(&g_tc.sensor_read_queue, &sensor_param);
			}
		} else {
			break;
		}
	}

	/* Histogram buffer processing and HW configuration. */
	hp3a_enable_histogram();
	/* AF stat buffer processing and HW configuration. */
	hp3a_enable_af();

	/* Notify threads waiting for stats. */
	complete(&g_tc.frame_done);
}

/**
 * hp3a_update_framework- execute tasks between frames.
 *
 * No return value.
 **/
void hp3a_update_stats_pipe_done(void)
{
	struct hp3a_internal_buffer *ibuffer;

	if (unlikely(g_tc.v4l2_streaming == 0)) {
		hp3a_disable_raw();
		return;
	}

	/* RAW stat buffer processing. */
	if (g_tc.raw_hw_configured == 1) {
		if ((++g_tc.raw_cap_sched_count) == g_tc.raw_frequency) {
			g_tc.raw_cap_sched_count = 0;
			ibuffer = NULL;
			if (hp3a_dequeue(&g_tc.raw_frame_queue, &ibuffer) == 0) {
				if (ibuffer->buffer_size >= g_tc.req_raw_buffer_size) {
					hp3a_enable_raw(ibuffer->isp_addr);
					ibuffer->type = BAYER;
					hp3a_enqueue(&g_tc.ready_stats_queue, &ibuffer);
				}
			}
		} else if (g_tc.raw_cap_sched_count == 1) {
			hp3a_disable_raw();
		}
	}
}

/**
 * hp3a_schedule_task - Schedule deffered tasks.
 *
 * No return value.
 **/
void hp3a_schedule_task(void)
{
	if (likely(g_tc.v4l2_streaming == 1)) {
		/* Schedule delayed task. */
		schedule_work(&g_hp3a_work_queue);
	}
}

/**
 * hp3a_task - Tasks body, executes once per frame.
 * @work: Pointer to the  hp3a work structure.
 *
 * No return value.
 **/
static void hp3a_task(struct work_struct *work)
{
	/* Place holder for deferred tasks. */
	struct hp3a_sensor_param_internal sensor_param;
	struct cam_sensor_settings sensor_settings = {
		.flags = 0,
		.exposure = 0,
		.gain = 0,
		.regs = 0,
		.reg_data = 0};
	u32 frame_index = (g_tc.frame_count+1);

	/**
	 * Setup exposure and gain for next frame.
	 */
	if (hp3a_dequeue(&g_tc.sensor_write_queue, &sensor_param) == 0) {
		sensor_settings.exposure = sensor_param.exposure;
		sensor_settings.gain = sensor_param.gain;

		if (sensor_param.exposure)
			sensor_settings.flags |= OMAP34XXCAM_SET_EXPOSURE;
		if (sensor_param.gain)
			sensor_settings.flags |= OMAP34XXCAM_SET_GAIN;

		/**
		* Write and read sensor settings.
		*/
		omap34xxcam_sensor_settings(sensor_param.v4l2_dev, &sensor_settings);

		if (g_tc.current_gain != sensor_settings.gain) {
			sensor_param.frame_id = (frame_index + g_tc.gain_sync);
			sensor_param.exposure = 0;
			sensor_param.gain = sensor_settings.gain;
			/* Queue new value for stats collecton. */
			hp3a_enqueue_irqsave(&g_tc.sensor_read_queue, &sensor_param);
			/* Save new programmed in gain value. */
			g_tc.current_gain = sensor_settings.gain;
		}

		if (g_tc.current_exposure != sensor_settings.exposure) {
			sensor_param.frame_id = (frame_index + g_tc.exposure_sync);
			sensor_param.exposure = sensor_settings.exposure;
			sensor_param.gain = 0;
			/* Queue new value for stats collecton. */
			hp3a_enqueue_irqsave(&g_tc.sensor_read_queue, &sensor_param);
			/* Save new programmed in exposure value. */
			g_tc.current_exposure = sensor_settings.exposure;
		}
	}
}
