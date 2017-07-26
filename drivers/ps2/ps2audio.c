/*
 *  Playstation 2 Sound Driver
 *  rom0:LIBSD and audsrv.irx need to be loaded on the IOP side.
 *
 *  Copyright (C) 2014 Juergen Urban
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/iopmodules.h>

#define AUDSRV_IRX 0x870884d

#define AUDSRV_INIT 0x0000
#define AUDSRV_QUIT 0x0001

#define AUDSRV_FORMAT_OK 0x0002
#define AUDSRV_SET_FORMAT 0x0003
#define AUDSRV_PLAY_AUDIO 0x0004
#define AUDSRV_WAIT_AUDIO 0x0005
#define AUDSRV_STOP_AUDIO 0x0006
#define AUDSRV_SET_VOLUME 0x0007
#define AUDSRV_SET_THRESHOLD 0x0008

#define AUDSRV_ERR_NOERROR 0
#define AUDSRV_ERR_NOT_INITIALIZED 1
#define AUDSRV_ERR_RPC_FAILED 2
#define AUDSRV_ERR_FORMAT_NOT_SUPPORTED 3
#define AUDSRV_ERR_OUT_OF_MEMORY 4
#define AUDSRV_ERR_ARGS 5
#define AUDSRV_ERR_NO_DISC 6
#define AUDSRV_ERR_NO_MORE_CHANNELS 7
#define AUDSRV_ERR_FAILED_TO_LOAD_ADPCM 16
#define AUDSRV_ERR_FAILED_TO_CREATE_SEMA 17

#define AUDSRV_FILLBUF_CALLBACK 0x0010

#define MAX_BUFFER (4 * 4096)

#define MAX_VOLUME 0x3FFF
#define MIN_VOLUME 0

#define RINGBUFFER_SIZE (2 * 4096)

#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef struct {
	struct snd_card *card;
	int rpc_initialized;
	snd_pcm_uframes_t samplessinceperiod;
	snd_pcm_uframes_t samplecounter;
	snd_pcm_uframes_t inputcounter;
	ps2sif_clientdata_t cd_audsrv_rpc;
	unsigned int *sbuff;
	int volume;
	struct snd_pcm_substream *substream;
	uint8_t *ringbuffer;
	volatile uint32_t readpos;
	volatile uint32_t writepos;
	volatile int starting;
	volatile int stopping;
	volatile int playing;
	struct workqueue_struct *wq;
	struct work_struct work;
} audsrv_data_t;

static struct snd_pcm_hardware ps2audio_pcm_hw = {
	.info             = SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_MMAP_VALID,
	.formats          = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8,
	.rates            = SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_22050 |
		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
	.rate_min         = 11025,
	.rate_max         = 48000,
	.channels_min     = 1,
	.channels_max     = 2,
	.buffer_bytes_max = RINGBUFFER_SIZE,
	.period_bytes_min = 1024,
	.period_bytes_max = 2048,
	.periods_min      = 1,
	.periods_max      = 40,
};

static void ps2audio_rpcend_notify(void *arg)
{
	complete((struct completion *)arg);
	return;
}

static int ps2audio_get_error(audsrv_data_t *data)
{
	switch (data->sbuff[0]) {
		case AUDSRV_ERR_NOERROR:
			return 0;
		case AUDSRV_ERR_OUT_OF_MEMORY:
			return -ENOMEM;
		case AUDSRV_ERR_ARGS:
			return -EINVAL;
		case AUDSRV_ERR_NO_DISC:
			return -ENXIO;
		case -AUDSRV_ERR_FORMAT_NOT_SUPPORTED:
		case AUDSRV_ERR_FORMAT_NOT_SUPPORTED:
			return -EINVAL;
		case AUDSRV_ERR_RPC_FAILED:
			return -EIO;
		case AUDSRV_ERR_NO_MORE_CHANNELS:
			return -ENOSPC;
		case AUDSRV_ERR_NOT_INITIALIZED:
			return -ENODEV;
		default:
			return -EIO;
	}
}

static int ps2audio_send_cmd(audsrv_data_t *data, unsigned int cmd, unsigned int arg)
{
	struct completion compl;
	int rv;

	data->sbuff[0] = arg;
	init_completion(&compl);
	do {
		rv = ps2sif_callrpc(&data->cd_audsrv_rpc,
			cmd,
			SIF_RPCM_NOWAIT,
			data->sbuff, sizeof(data->sbuff[0]),
			data->sbuff, sizeof(data->sbuff[0]),
			ps2audio_rpcend_notify,
			&compl);
	} while (rv == -E_SIF_PKT_ALLOC);
	if (rv == 0) {
		wait_for_completion(&compl);
		return ps2audio_get_error(data);
	} else {
		return -ENOMEM;
	}
}

static int ps2audio_pcm_open(struct snd_pcm_substream *ss)
{
	audsrv_data_t *data = snd_pcm_substream_chip(ss);

	ss->runtime->hw = ps2audio_pcm_hw;
	ss->private_data = data;
	data->samplecounter = 0;
	data->inputcounter = 0;
	data->starting = 0;
	data->stopping = 0;
	data->readpos = 0;
	data->writepos = 0;

	return 0;
}

static int ps2audio_pcm_close(struct snd_pcm_substream *ss)
{
	audsrv_data_t *data = snd_pcm_substream_chip(ss);
	int ret;

	data->playing = 0;
	flush_workqueue(data->wq);

	/* Ensure that no audio is playing after close. */
	ret = ps2audio_send_cmd(data, AUDSRV_STOP_AUDIO, 0);
	if (ret != 0) {
		printk(KERN_ERR "ps2audio: %s:%d %s() ret = %d\n", __FILE__, __LINE__, __FUNCTION__, ret);
	}

	ss->private_data = NULL;

	return 0;
}

static int ps2audio_hw_params(struct snd_pcm_substream *ss,
	struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(ss, params_buffer_bytes(hw_params));
}

static int ps2audio_hw_free(struct snd_pcm_substream *ss)
{
	return snd_pcm_lib_free_pages(ss);
}

/** Set PCM format which should be played. */
static int ps2audio_set_format(audsrv_data_t *data, unsigned int freq, unsigned int bits, unsigned int channels)
{
	struct completion compl;
	int rv;

	data->sbuff[0] = freq;
	data->sbuff[1] = bits;
	data->sbuff[2] = channels;
	init_completion(&compl);
	do {
		rv = ps2sif_callrpc(&data->cd_audsrv_rpc,
			AUDSRV_SET_FORMAT,
			SIF_RPCM_NOWAIT,
			data->sbuff, 3 * sizeof(data->sbuff[0]),
			data->sbuff, sizeof(data->sbuff[0]),
			ps2audio_rpcend_notify,
			&compl);
	} while (rv == -E_SIF_PKT_ALLOC);
	if (rv == 0) {
		wait_for_completion(&compl);
		return ps2audio_get_error(data);
	} else {
		return -ENOMEM;
	}
}

static int ps2audio_pcm_prepare(struct snd_pcm_substream *ss)
{
	audsrv_data_t *data = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *runtime;
	int ret;

	/* Wait until previous transfers are finished. */
	flush_workqueue(data->wq);

	runtime = ss->runtime;
	ret = ps2audio_set_format(data, runtime->rate, runtime->sample_bits, runtime->channels);

	if (ret == 0) {
		ret = ps2audio_send_cmd(data, AUDSRV_SET_VOLUME, data->volume);
		if (ret != 0) {
			printk(KERN_ERR "ps2audio: %s:%d %s() ret = %d\n", __FILE__, __LINE__, __FUNCTION__, ret);
		}
	}

	return ret;
}

static int ps2audio_pcm_trigger(struct snd_pcm_substream *ss, int cmd)
{
	int ret = 0;
	audsrv_data_t *data = snd_pcm_substream_chip(ss);

	switch(cmd)
	{
		case SNDRV_PCM_TRIGGER_START:
			if (!data->starting && !data->stopping) {
				data->substream = ss;
				data->starting = 1;
				data->playing = 1;

				queue_work(data->wq, &data->work);
			} else {
				ret = -EWOULDBLOCK;
			}
			break;

		case SNDRV_PCM_TRIGGER_STOP:
			if (!data->stopping) {

				data->stopping = 1;
				data->playing = 0;

				queue_work(data->wq, &data->work);
			} else {
				ret = -EWOULDBLOCK;
			}
			break;

		default:
			ret = -EINVAL;
			break;
	}
	return ret;
}

static snd_pcm_uframes_t ps2audio_pcm_pointer(struct snd_pcm_substream *ss)
{
	audsrv_data_t *data = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *runtime = ss->runtime;
	snd_pcm_uframes_t rv;

	/* Return the current read position of the workqueue. */
	rv = bytes_to_frames(runtime, data->readpos);
	return rv;
}

static int ps2audio_pcm_copy(struct snd_pcm_substream *ss, int channel,
	snd_pcm_uframes_t pos, void __user *buf, snd_pcm_uframes_t count)
{
	audsrv_data_t *data = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *runtime = ss->runtime;
	uint32_t bytepos;
	uint32_t size;
	int rv;

	/* Copy PCM data to ringbuffer. */
	bytepos = frames_to_bytes(runtime, pos);
	size = frames_to_bytes(runtime, count);
	rv = copy_from_user(data->ringbuffer + bytepos, buf, size);
	if (rv >= 0) {
		data->writepos = (bytepos + size) % runtime->hw.buffer_bytes_max;
		data->inputcounter += count;
	}
	return rv;
}

static struct page *ps2audio_pcm_page(struct snd_pcm_substream *ss,
				   unsigned long offset)
{
	audsrv_data_t *data = snd_pcm_substream_chip(ss);

	return vmalloc_to_page(data->ringbuffer + offset);
}

static struct snd_pcm_ops ps2audio_pcm_ops = {
	.open      = ps2audio_pcm_open,
	.close     = ps2audio_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = ps2audio_hw_params,
	.hw_free   = ps2audio_hw_free,
	.prepare   = ps2audio_pcm_prepare,
	.trigger   = ps2audio_pcm_trigger,
	.pointer   = ps2audio_pcm_pointer,
	.copy      = ps2audio_pcm_copy,
	.page      = ps2audio_pcm_page,
};

static int __devinit ps2audio_bind(audsrv_data_t *data)
{
	int loop;
	struct completion compl;
	int rv;

	if (data->rpc_initialized) {
		return 0;
	}

	init_completion(&compl);

	/* Bind audsrv.irx module */
	for (loop = 10; loop; loop--) {
		rv = ps2sif_bindrpc(&data->cd_audsrv_rpc, AUDSRV_IRX,
			SIF_RPCM_NOWAIT, ps2audio_rpcend_notify, (void *)&compl);
		if (rv < 0) {
			break;
		}
		wait_for_completion(&compl);
		if (data->cd_audsrv_rpc.serve != 0)
			break;
		/* Wait some time. */
		schedule_timeout(HZ);
	}
	if (data->cd_audsrv_rpc.serve == 0) {
		printk(KERN_ERR "ps2audio: bind error 1.\n");
		return -EIO;
	}
	data->rpc_initialized = -1;
	return 0;
}

static int ps2audio_pb_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = MIN_VOLUME;
	uinfo->value.integer.max = MAX_VOLUME;
	uinfo->value.integer.step = 1;

	return 0;
}

static int ps2audio_pb_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *uvalue)
{
	audsrv_data_t *data;

	data = kcontrol->private_data;

	uvalue->value.integer.value[0] = data->volume;

	return 0;
}

static int ps2audio_pb_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *uvalue)
{
	audsrv_data_t *data;
	int volume;
	int rv;

	data = kcontrol->private_data;
	volume = uvalue->value.integer.value[0];
	if (volume > MAX_VOLUME) {
		return -EINVAL;
	} else if (volume < MIN_VOLUME) {
		return -EINVAL;
	}

	rv = ps2audio_send_cmd(data, AUDSRV_SET_VOLUME, volume);
	if (rv == 0) {
		data->volume = volume;
	}
	return rv;
}

static struct snd_kcontrol_new ps2audio_playback_vol = {
	/* Control is of type MIXER */
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PS2 Audio Volume",
	.index = 0,
	.info = ps2audio_pb_vol_info,
	.get = ps2audio_pb_vol_get,
	.put = ps2audio_pb_vol_put,
};

/** Transfer PCM data to the aussrv.irx which is devided into 2 buffers, because of ringbuffer wrap around. */
static int play_audio(audsrv_data_t *data, void *buf1, uint32_t size1, void *buf2, uint32_t size2)
{
	int rv;
	struct completion compl;
	uint8_t *p1;

	data->sbuff[0] = size1 + size2;
	p1 = (void *) &data->sbuff[1];
	memcpy(p1, buf1, size1);
	if ((buf2 != NULL) && (size2 > 0)) {
		uint8_t *p2;

		p2 = p1 + size1;
		memcpy(p2, buf2, size2);
	}
	init_completion(&compl);
	do {
		rv = ps2sif_callrpc(&data->cd_audsrv_rpc,
			AUDSRV_PLAY_AUDIO,
			SIF_RPCM_NOWAIT,
			data->sbuff, size1 + size2 + sizeof(data->sbuff[0]),
			data->sbuff, sizeof(data->sbuff[0]),
			ps2audio_rpcend_notify,
			&compl);
	} while (rv == -E_SIF_PKT_ALLOC);
	if (rv == 0) {
		wait_for_completion(&compl);
		return data->sbuff[0];
	} else {
		return -ENOMEM;
	}
}

/** Work queue which plays the audio. */
static void play_audio_wq(struct work_struct *work)
{
	audsrv_data_t *data = container_of(work, audsrv_data_t, work);
	struct snd_pcm_substream *ss;
	struct snd_pcm_runtime *runtime;

	ss = data->substream;

	if (data->starting) {
		/* Start playing audio. */
		data->samplessinceperiod = 0;
		data->starting = 0;
	}

	runtime = ss->runtime;
	if (data->playing && (runtime != NULL)) {
		uint32_t period_size;

		/* Loop which plays the PCM data from the ringbuffer. */
		period_size = frames_to_bytes(runtime, runtime->period_size);
		while (data->playing) {
			uint32_t size1;
			uint32_t size2;
			int ret;

			/* The following call will block until data can be transferred to the audsrv.irx module. */
			ret = ps2audio_send_cmd(data, AUDSRV_WAIT_AUDIO, period_size);
			if (ret < 0) {
				printk(KERN_ERR "ps2audio: %s:%d %s() Wait for Audio failed\n", __FILE__, __LINE__, __FUNCTION__);
			}

			/* Expect that ringbuffer was filled fast enough. */
			size1 = runtime->hw.buffer_bytes_max - data->readpos;
			size2 = 0;

			size1 = MIN(size1, period_size);
			size2 = period_size - size1;

			/* Copy PCM data to audsrv,irx module. */
			ret = play_audio(data, data->ringbuffer + data->readpos, size1, data->ringbuffer, size2);
			if (ret != period_size) {
				printk(KERN_ERR "ps2audio: %s:%d %s() play_audio() failed %d != %d\n", __FILE__, __LINE__, __FUNCTION__, ret, period_size);
			}
			data->readpos = (data->readpos + period_size) % runtime->hw.buffer_bytes_max;
			if (data->playing) {
				snd_pcm_period_elapsed(ss);
			}
		}
	}

	if (data->stopping) {
		int ret;

		/* Stop playing audio. */
		ret = ps2audio_send_cmd(data, AUDSRV_STOP_AUDIO, 0);
		if (ret != 0) {
			printk(KERN_ERR "ps2audio: %s:%d %s() ret = %d\n", __FILE__, __LINE__, __FUNCTION__, ret);
		}
		data->readpos = data->writepos;
		data->stopping = 0;
	}

}

static struct snd_device_ops ops = { NULL };

static int __devinit ps2audio_probe(struct platform_device *dev)
{
	struct snd_card *card;
	int ret;
	audsrv_data_t *data;
	struct snd_pcm *pcm;

	if (load_module_firmware("ps2/freesd.irx", 0) < 0) {
		printk("ps2audio: loading ps2/freesd.irx failed\n");
		return -ENODEV;
	}

	if (load_module_firmware("ps2/audsrv.irx", 0) < 0) {
		printk("ps2audio: loading ps2/audsrv.irx failed\n");
		return -ENODEV;
	}

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		return -ENOMEM;
	}
	memset(data, 0, sizeof(*data));
	data->volume = MAX_VOLUME;
	data->sbuff = kmalloc(MAX_BUFFER, GFP_KERNEL);
	if (data->sbuff == NULL) {
		kfree(data);
		return -ENOMEM;
	}
	memset(data->sbuff, 0, MAX_BUFFER);
	/* Detect the audsrv.irx driver. */
	ret = ps2audio_bind(data);
	if (ret < 0) {
		kfree(data);
		return ret;
	}
	/* Initialize the IRX module. */
	ret = ps2audio_send_cmd(data, AUDSRV_INIT, 0);
	if (ret < 0) {
		printk("ps2audio: Failed to initialize audsrv.irx.\n");
		kfree(data);
		return ret;
	}

	ret = snd_card_create(SNDRV_DEFAULT_IDX1, "ps2audio",
		THIS_MODULE, 0, &card);
	if (ret < 0) {
		kfree(data);
		return ret;
	}

	data->card = card;
	platform_set_drvdata(dev, data);

	strcpy(card->driver, "ps2audio");
	strcpy(card->shortname, "PS2 Audio");
	strcpy(card->longname, "Sound for the Sony Playstation 2");
	snd_card_set_dev(card, &dev->dev);

	ret = snd_device_new(card, SNDRV_DEV_LOWLEVEL, data, &ops);
	if (ret < 0) {
		snd_card_free(card);
		kfree(data);
		return ret;
	}

	if ((ret = snd_card_register(card)) < 0) {
		snd_card_free(card);
		kfree(data);
		return ret;
	}

	ret = snd_pcm_new(card, card->driver, 0, 1, 0, &pcm);
	if (ret < 0) {
		snd_card_free(card);
		kfree(data);
		return ret;
	}

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &ps2audio_pcm_ops);
	pcm->private_data = data;
	pcm->info_flags = 0;
	strcpy(pcm->name, card->shortname);

	ret = snd_pcm_lib_preallocate_pages_for_all(pcm,
		SNDRV_DMA_TYPE_CONTINUOUS, snd_dma_continuous_data(GFP_KERNEL), MAX_BUFFER, MAX_BUFFER);
	if (ret < 0) {
		snd_card_free(card);
		kfree(data);
		return ret;
	}

	ret = snd_ctl_add(card, snd_ctl_new1(&ps2audio_playback_vol, data));
	if (ret < 0) {
		snd_card_free(card);
		kfree(data);
		return ret;
	}

	data->ringbuffer = vmalloc(RINGBUFFER_SIZE);
	if (data->ringbuffer == NULL) {
		snd_card_free(card);
		kfree(data);
		return -ENOMEM;
	}
	data->readpos = 0;
	data->writepos = 0;
	data->wq = create_workqueue("ps2audio");
	INIT_WORK(&data->work,(void *) play_audio_wq);
	ret = snd_device_register(card, pcm);
	if (ret < 0) {
		snd_card_free(card);
		kfree(data);
		return ret;
	}
	printk(KERN_INFO "ps2audio: Playstation 2 Sound initialised.\n");

	return 0;
}

static int __devexit ps2audio_driver_remove(struct platform_device *pdev)
{
	audsrv_data_t *data = platform_get_drvdata(pdev);

	if (data != NULL) {
		snd_card_free(data->card);

		if (data->sbuff != NULL) {
			int ret;

			flush_workqueue(data->wq);

			ret = ps2audio_send_cmd(data, AUDSRV_QUIT, 0);
			if (ret < 0) {
				printk("ps2audio: Failed to stop audsrv.irx.\n");
			}
			kfree(data->sbuff);
			data->sbuff = NULL;
			vfree(data->ringbuffer);
			data->ringbuffer = NULL;

			destroy_workqueue(data->wq);
		}
		kfree(data);
	}

	return 0;
}

static struct platform_driver ps2audio_driver = {
	.probe	= ps2audio_probe,
	.remove	= __devexit_p(ps2audio_driver_remove),
	.driver	= {
		.name	= "ps2audio",
		.owner	= THIS_MODULE,
	},
};

static int __init ps2audio_init(void)
{
	return platform_driver_register(&ps2audio_driver);
}

static void __exit ps2audio_exit(void)
{
	platform_driver_unregister(&ps2audio_driver);
	return;
}

module_init(ps2audio_init);
module_exit(ps2audio_exit);

MODULE_AUTHOR("Juergen Urban");
MODULE_DESCRIPTION("PlayStation 2 Audio Driver using audsrv.irx");
MODULE_LICENSE("GPL");
