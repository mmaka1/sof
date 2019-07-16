// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.

#include <sof/audio/component.h>
#include <sof/lib/memory.h>
#include <sof/lib/uuid.h>

/* 1d501197-da27-4697-80c8-4e694d3600a0 */
DECLARE_SOF_RT_UUID("amp", amp_uuid, 0x1d501197, 0xda27, 0x4697,
		    0x80, 0xc8, 0x4e, 0x69, 0x4d, 0x36, 0x00, 0xa0);

DECLARE_TR_CTX(amp_tr, SOF_UUID(amp_uuid), LOG_LEVEL_INFO);

struct amp_comp_data {
	int placeholder;
};

static struct comp_dev *amp_new(const struct comp_driver *drv,
				struct sof_ipc_comp *comp)
{
	struct comp_dev *dev;
	struct sof_ipc_comp_process *amp;
	struct sof_ipc_comp_process *ipc_amp =
			(struct sof_ipc_comp_process *)comp;
	struct amp_comp_data *cd;
	int ret;

	dev = comp_alloc(drv, COMP_SIZE(struct sof_ipc_comp_process));
	if (!dev)
		return NULL;

	cd = rzalloc(SOF_MEM_ZONE_RUNTIME, 0, SOF_MEM_CAPS_RAM, sizeof(*cd));
	if (!cd) {
		rfree(dev);
		return NULL;
	}

	amp = COMP_GET_IPC(dev, sof_ipc_comp_process);
	ret = memcpy_s(amp, sizeof(*amp), ipc_amp,
		       sizeof(struct sof_ipc_comp_process));
	assert(!ret);

	comp_set_drvdata(dev, cd);

	dev->state = COMP_STATE_READY;

	comp_dbg(dev, "amplifier created");

	return dev;
}

static void amp_free(struct comp_dev *dev)
{
	struct amp_comp_data *cd = comp_get_drvdata(dev);

	rfree(cd);
	rfree(dev);
}

static int amp_trigger(struct comp_dev *dev, int cmd)
{
	comp_dbg(dev, "amplifier got trigger cmd %d", cmd);
	return comp_set_state(dev, cmd);
}

static int amp_prepare(struct comp_dev *dev)
{
	int ret;
	struct comp_buffer *sink_buf;
	struct sof_ipc_comp_config *config = dev_comp_config(dev);
	uint32_t sink_per_bytes;

	ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
	if (ret < 0)
		return ret;

	if (ret == COMP_STATUS_STATE_ALREADY_SET)
		return PPL_STATUS_PATH_STOP;

	sink_buf = list_first_item(&dev->bsink_list,
				   struct comp_buffer, source_list);

	sink_per_bytes = audio_stream_period_bytes(&sink_buf->stream,
						   dev->frames);

	if (sink_buf->stream.size < config->periods_sink * sink_per_bytes) {
		comp_err(dev, "amp_prepare(): sink buffer size is insufficient");
		return -ENOMEM;
	}

	comp_dbg(dev, "amplifier prepared");

	return 0;
}

static int amp_reset(struct comp_dev *dev)
{
	return comp_set_state(dev, COMP_TRIGGER_RESET);
}

static int amp_copy(struct comp_dev *dev)
{
	struct comp_copy_limits cl;
	struct comp_buffer *source;
	struct comp_buffer *sink;
	int frame;
	int channel;
	uint32_t buff_frag = 0;
	int16_t *src;
	int16_t *dst;

	source = list_first_item(&dev->bsource_list, struct comp_buffer,
				 sink_list);
	sink = list_first_item(&dev->bsink_list, struct comp_buffer,
			       source_list);

	comp_get_copy_limits_with_lock(source, sink, &cl);

	buffer_invalidate(source, cl.source_bytes);

	for (frame = 0; frame < cl.frames; frame++) {
		for (channel = 0; channel < sink->stream.channels; channel++) {
			src = audio_stream_read_frag_s16(&source->stream,
							 buff_frag);
			dst = audio_stream_write_frag_s16(&sink->stream,
							  buff_frag);
			*dst = *src;
			++buff_frag;
		}
	}

	buffer_writeback(sink, cl.sink_bytes);

	comp_update_buffer_produce(sink, cl.sink_bytes);
	comp_update_buffer_consume(source, cl.source_bytes);

	return 0;
}

static const struct comp_driver comp_amp = {
	.uid = SOF_RT_UUID(amp_uuid),
	.tctx = &amp_tr,
	.ops = {
		.create = amp_new,
		.free = amp_free,
		.params = NULL,
		.cmd = NULL,
		.trigger = amp_trigger,
		.prepare = amp_prepare,
		.reset = amp_reset,
		.copy = amp_copy,
	},
};

static SHARED_DATA struct comp_driver_info comp_amp_info = {
	.drv = &comp_amp,
};

static void sys_comp_amp_init(void)
{
	comp_register(platform_shared_get(&comp_amp_info,
					  sizeof(comp_amp_info)));
}

DECLARE_MODULE(sys_comp_amp_init);
