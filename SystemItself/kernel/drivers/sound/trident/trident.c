/*
 *
 *	Trident 4D-Wave/SiS 7018/ALi 5451 OSS driver for Linux 2.2.x
 *
 *	Driver: Alan Cox <alan@redhat.com>
 *
 *  Built from:
 *	Low level code: <audio@tridentmicro.com> from ALSA
 *	Framework: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *	Extended by: Zach Brown <zab@redhat.com>  
 *
 *  Hacked up by:
 *	Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *	Ollie Lho <ollie@sis.com.tw> SiS 7018 Audio Core Support
 *	Ching Ling Lee <cling-li@ali.com.tw> ALi 5451 Audio Core Support 
 *	Michael Krueger <invenies@web.de> Port to AtheOS/Syllable
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  History
 *  v0.14.5c Oct 28 2000 Alan Cox
 *      Merged Ching-Ling Lee's ADC/DAC fix from the newer ALI work
 *  v0.14.5b Sept 17 2000 Eric Brombaugh
 *      Fix hang when trying ALi MIDI init on Trident
 *  v0.14.5 June 3 2000 Ollie Lho
 *  	Misc bug fix from the Net
 *	Backported from 2.4.0-test1 to 2.2.16 (Eric Brombaugh)
 *  v0.14.4 May 20 2000 Aaron Holtzman
 *  	Fix kfree'd memory access in release
 *  	Fix race in open while looking for a free virtual channel slot
 *  	remove open_wait wq (which appears to be unused)
 *  v0.14.3 May 10 2000 Ollie Lho
 *	fixed a small bug in trident_update_ptr, xmms 1.0.1 no longer uses 100% CPU
 *  v0.14.2 Mar 29 2000 Ching Ling Lee
 *	Add clear to silence advance in trident_update_ptr 
 *	fix invalid data of the end of the sound
 *  v0.14.1 Mar 24 2000 Ching Ling Lee
 *	ALi 5451 support added, playback and recording O.K.
 *	ALi 5451 originally developed and structured based on sonicvibes, and
 *	suggested to merge into this file by Alan Cox.
 *  v0.14 Mar 15 2000 Ollie Lho
 *	5.1 channel output support with channel binding. What's the Matrix ?
 *  v0.13.1 Mar 10 2000 Ollie Lho
 *	few minor bugs on dual codec support, needs more testing
 *  v0.13 Mar 03 2000 Ollie Lho
 *	new pci_* for 2.4 kernel, back ported to 2.2
 *  v0.12 Feb 23 2000 Ollie Lho
 *	Preliminary Recording support
 *  v0.11.2 Feb 19 2000 Ollie Lho
 *	removed incomplete full-dulplex support
 *  v0.11.1 Jan 28 2000 Ollie Lho
 *	small bug in setting sample rate for 4d-nx (reported by Aaron)
 *  v0.11 Jan 27 2000 Ollie Lho
 *	DMA bug, scheduler latency, second try
 *  v0.10 Jan 24 2000 Ollie Lho
 *	DMA bug fixed, found kernel scheduling problem
 *  v0.09 Jan 20 2000 Ollie Lho
 *	Clean up of channel register access routine (prepare for channel binding)
 *  v0.08 Jan 14 2000 Ollie Lho
 *	Isolation of AC97 codec code
 *  v0.07 Jan 13 2000 Ollie Lho
 *	Get rid of ugly old low level access routines (e.g. CHRegs.lp****)
 *  v0.06 Jan 11 2000 Ollie Lho
 *	Preliminary support for dual (more ?) AC97 codecs
 *  v0.05 Jan 08 2000 Luca Montecchiani <m.luca@iname.com>
 *	adapt to 2.3.x new __setup/__init call
 *  v0.04 Dec 31 1999 Ollie Lho
 *	Multiple Open, using Middle Loop Interrupt to smooth playback
 *  v0.03 Dec 24 1999 Ollie Lho
 *	mem leak in prog_dmabuf and dealloc_dmabuf removed
 *  v0.02 Dec 15 1999 Ollie Lho
 *	SiS 7018 support added, playback O.K.
 *  v0.01 Alan Cox et. al.
 *	Initial Release in kernel 2.3.30, does not work
 * 
 *  ToDo
 *	Clean up of low level channel register access code. (done)
 *	Fix the bug on dma buffer management in update_ptr, read/write, drain_dac (done)
 *	Dual AC97 codecs support (done)
 *	Recording support (done)
 *	Mmap support
 *	"Channel Binding" ioctl extension (done)
 *	new pci device driver interface for 2.4 kernel (done)
 */

#include <posix/errno.h>
#include <posix/ioctls.h>
#include <posix/fcntl.h>
#include <posix/termios.h>
#include <posix/signal.h>

#include <atheos/kernel.h>
#include <atheos/kdebug.h>
#include <atheos/areas.h>
#include <atheos/pci.h>
#include <atheos/device.h>
#include <atheos/semaphore.h>
#include <atheos/irq.h>
#include <atheos/isa_io.h>
#include <atheos/dma.h>
#include <atheos/udelay.h>
#include <atheos/areas.h>
#include <atheos/soundcard.h>
#include <atheos/spinlock.h>
#include <atheos/timer.h>
#include <atheos/bitops.h>
#include <atheos/linux_compat.h>
#include <macros.h>

#include "ac97_codec.h"

#include "trident.h"

#define DRIVER_VERSION "0.1pre1"

/* magic numbers to protect our data structures */
#define TRIDENT_CARD_MAGIC	0x5072696E /* "Prin" */
#define TRIDENT_STATE_MAGIC	0x63657373 /* "cess" */

#define TRIDENT_DMA_MASK	0x3fffffff /* DMA buffer mask for pci_alloc_consist */

#define NR_HW_CH		32

/* maxinum nuber of AC97 codecs connected, AC97 2.0 defined 4, but 7018 and 4D-NX only
   have 2 SDATA_IN lines (currently) */
#define NR_AC97		2

/* minor number of /dev/dspW */
#define SND_DEV_DSP8	3

/* minor number of /dev/dspW */
#define SND_DEV_DSP16	5 

/* minor number of /dev/swmodem (temporary, experimental) */
#define SND_DEV_SWMODEM	7

/* Size of the MIDI buffer */
#define MIDIINBUF	256
#define MIDIOUTBUF	256

/* AtheOS additions */
static void *trident_card_data;
static void *trident_state_data;

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

static const char invalid_magic[] = KERN_CRIT "trident: invalid magic value in %s\n";

struct pci_audio_info {
	u16 vendor;
	u16 device;
	char *name;
};

static struct pci_audio_info pci_audio_devices[] = {
	{PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_TRIDENT_4DWAVE_DX, "Trident 4DWave DX"},
	{PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_TRIDENT_4DWAVE_NX, "Trident 4DWave NX"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_7018, "SiS 7018 PCI Audio"},
	{PCI_VENDOR_ID_ALI, PCI_DEVICE_ID_ALI_5451, "ALi Audio Accelerator"}
};


/* "software" or virtual channel, an instance of opened /dev/dsp */
struct trident_state {
	unsigned int magic;
	struct trident_card *card;	/* Card info */

	/* file mode */
	mode_t open_mode;

	/* virtual channel number */
	int virt;

	struct dmabuf {
		/* wave sample stuff */
		unsigned int rate;
		unsigned char fmt, enable;

		/* hardware channel */
		struct trident_channel *channel;

		/* OSS buffer management stuff */
		void *rawbuf;
		dma_addr_t dma_handle;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;

		/* our buffer acts like a circular ring */
		unsigned hwptr;		/* where dma last started, updated by update_ptr */
		unsigned swptr;		/* where driver last clear/filled, updated by read/write */
		int count;		/* bytes to be comsumed or been generated by dma machine */
		unsigned total_bytes;	/* total bytes dmaed by hardware */

		unsigned error;		/* number of over/underruns */
		//struct wait_queue *wait;	/* put process on wait queue when no more space in buffer */
		sem_id wait;

		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;

		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned endcleared:1;
		unsigned update_flag;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
	} dmabuf;
};

/* hardware channels */
struct trident_channel {
	int  num;	/* channel number */
	u32 lba;	/* Loop Begine Address, where dma buffer starts */
	u32 eso;	/* End Sample Offset, wehre dma buffer ends (in the unit of samples) */
	u32 delta;	/* delta value, sample rate / 48k for playback, 48k/sample rate for recording */
	u16 attribute;	/* control where PCM data go and come  */
	u16 fm_vol;
	u32 control;	/* signed/unsigned, 8/16 bits, mono/stereo */
};

struct trident_pcm_bank_address {
	u32 start;
	u32 stop;
	u32 aint;
	u32 aint_en;
};
static struct trident_pcm_bank_address bank_a_addrs =
{
	T4D_START_A,
	T4D_STOP_A,
	T4D_AINT_A,
	T4D_AINTEN_A
};
static struct trident_pcm_bank_address bank_b_addrs =
{
	T4D_START_B,
	T4D_STOP_B,
	T4D_AINT_B,
	T4D_AINTEN_B
};
struct trident_pcm_bank {
	/* register addresses to control bank operations */
	struct trident_pcm_bank_address *addresses;
	/* each bank has 32 channels */
	u32 bitmap; /* channel allocation bitmap */
	struct trident_channel channels[32];
};

struct trident_card {
	unsigned int magic;

	/* We keep trident cards in a linked list */
	struct trident_card *next;

	/* single open lock mechanism, only used for recording */
	sem_id open_sem;

	/* The trident has a certain amount of cross channel interaction
	   so we use a single per card lock */
	SpinLock_s lock;

	/* PCI device stuff */
	struct pci_audio_info *pci_info;
	PCI_Info_s * pci_dev;
	u16 pci_id;
	u8 revision;

	/* soundcore stuff */
	int dev_audio;
	
	/* AtheOS add-on */
	int nDeviceID;
	
	/* structures for abstraction of hardware facilities, codecs, banks and channels*/
	struct ac97_codec *ac97_codec[NR_AC97];
	struct trident_pcm_bank banks[NR_BANKS];
	struct trident_state *states[NR_HW_CH];

	/* hardware resources */
	unsigned long iobase;
	unsigned int irq;
	
	/* Function support */
	struct trident_channel *(*alloc_pcm_channel)(struct trident_card *);
	struct trident_channel *(*alloc_rec_pcm_channel)(struct trident_card *);
	void (*free_pcm_channel)(struct trident_card *, int chan);
	void (*address_interrupt)(struct trident_card *);
};

static struct trident_card *devs = NULL;

static void ali_ac97_set(struct ac97_codec *codec, u8 reg, u16 val);
static u16 ali_ac97_get(struct ac97_codec *codec, u8 reg);

static void trident_ac97_set(struct ac97_codec *codec, u8 reg, u16 val);
static u16 trident_ac97_get(struct ac97_codec *codec, u8 reg);

static int trident_dsp_read(void *node, void *cookie, off_t ppos, void *buffer, size_t count);
static int trident_dsp_write(void *node, void *cookie, off_t ppos, const void *buffer, size_t count);
static int trident_dsp_ioctl (void *node, void *cookie, uint32 cmd, void *args, bool b_fromkernel);
static status_t trident_dsp_open (void* pNode, uint32 nFlags, void **pCookie );
static status_t trident_dsp_release( void* pNode, void* pCookie);

static int trident_mixer_ioctl (void *node, void *cookie, uint32 cmd, void *args, bool b_fromkernel);
static status_t trident_mixer_open (void* pNode, uint32 nFlags, void **pCookie );
static status_t trident_mixer_release( void* pNode, void* pCookie);

static int trident_enable_loop_interrupts(struct trident_card * card)
{
	u32 global_control;

	global_control = inl(TRID_REG(card, T4D_LFO_GC_CIR));

	switch (card->pci_id)
	{
	case PCI_DEVICE_ID_SI_7018:
		global_control |= (ENDLP_IE | MIDLP_IE| BANK_B_EN);
		break;
	case PCI_DEVICE_ID_ALI_5451:
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		global_control |= (ENDLP_IE | MIDLP_IE);
		break;
	default:
		return FALSE;
	}

	outl(global_control, TRID_REG(card, T4D_LFO_GC_CIR));

#ifdef DEBUG
	printk("trident: Enable Loop Interrupts, globctl = 0x%08X\n",
	       global_control);
#endif
	return (TRUE);
}

static int trident_disable_loop_interrupts(struct trident_card * card)
{
	u32 global_control;

	global_control = inl(TRID_REG(card, T4D_LFO_GC_CIR));
	global_control &= ~(ENDLP_IE | MIDLP_IE);
	outl(global_control, TRID_REG(card, T4D_LFO_GC_CIR));

#ifdef DEBUG
	printk("trident: Disabled Loop Interrupts, globctl = 0x%08X\n",
	       global_control);
#endif
	return (TRUE);
}

static void trident_enable_voice_irq(struct trident_card * card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 reg, addr = bank->addresses->aint_en;

	reg = inl(TRID_REG(card, addr));
	reg |= mask;
	outl(reg, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_AINTEN_B));
	printk("trident: enabled IRQ on channel %d, AINTEN_B = 0x%08x\n",
	       channel, reg);
#endif
}

static void trident_disable_voice_irq(struct trident_card * card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 reg, addr = bank->addresses->aint_en;
	
	reg = inl(TRID_REG(card, addr));
	reg &= ~mask;
	outl(reg, TRID_REG(card, addr));
	
	/* Ack the channel in case the interrupt was set before we disable it. */
	outl(mask, TRID_REG(card, bank->addresses->aint));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_AINTEN_B));
	printk("trident: disabled IRQ on channel %d, AINTEN_B = 0x%08x\n",
	       channel, reg);
#endif
}

static void trident_start_voice(struct trident_card * card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 addr = bank->addresses->start;

#ifdef DEBUG
	u32 reg;
#endif

	outl(mask, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_START_B));
	printk("trident: start voice on channel %d, START_B  = 0x%08x\n",
	       channel, reg);
#endif
}

static void trident_stop_voice(struct trident_card * card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 addr = bank->addresses->stop;

#ifdef DEBUG
	u32 reg;
#endif

	outl(mask, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_STOP_B));
	printk("trident: stop voice on channel %d,  STOP_B  = 0x%08x\n",
	       channel, reg);
#endif
}

static u32 trident_get_interrupt_mask (struct trident_card * card, unsigned int channel)
{
	struct trident_pcm_bank *bank = &card->banks[channel];
	u32 addr = bank->addresses->aint;
	return inl(TRID_REG(card, addr));
}

static int trident_check_channel_interrupt(struct trident_card * card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	u32 reg = trident_get_interrupt_mask (card, channel >> 5);

#ifdef DEBUG
	if (reg & mask)
		printk("trident: channel %d has interrupt, AINT_B = 0x%08x\n",
		       channel, reg);
#endif
	return (reg & mask) ? TRUE : FALSE;
}

static void trident_ack_channel_interrupt(struct trident_card * card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 reg, addr = bank->addresses->aint;

	reg = inl(TRID_REG(card, addr));
	reg &= mask;
	outl(reg, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_AINT_B));
	printk("trident: Ack channel %d interrupt, AINT_B = 0x%08x\n",
	       channel, reg);
#endif
}

static struct trident_channel * trident_alloc_pcm_channel(struct trident_card *card)
{
	struct trident_pcm_bank *bank;
	int idx;

	bank = &card->banks[BANK_B];

	for (idx = 31; idx >= 0; idx--) {
		if (!(bank->bitmap & (1 << idx))) {
			struct trident_channel *channel = &bank->channels[idx];
			bank->bitmap |= 1 << idx;
			channel->num = idx + 32;
			return channel;
		}
	}

	/* no more free channels avaliable */
	printk(KERN_ERR "trident: no more channels available on Bank B.\n");
	return NULL;
}

static struct trident_channel *ali_alloc_pcm_channel(struct trident_card *card)
{
	struct trident_pcm_bank *bank;
	int idx;

	bank = &card->banks[BANK_A];

	for (idx = ALI_PCM_OUT_CHANNEL_FIRST; idx <= ALI_PCM_OUT_CHANNEL_LAST ; idx++) {
		if (!(bank->bitmap & (1 << idx))) {
			struct trident_channel *channel = &bank->channels[idx];
			bank->bitmap |= 1 << idx;
			channel->num = idx;
			return channel;
		}
	}

	/* no more free channels avaliable */
	printk(KERN_ERR "trident: no more channels available on Bank B.\n");
	return NULL;
}

static struct trident_channel *ali_alloc_rec_pcm_channel(struct trident_card *card)
{
	struct trident_pcm_bank *bank;
	int idx = ALI_PCM_IN_CHANNEL;

	bank = &card->banks[BANK_A];
	
	if (!(bank->bitmap & (1 << idx))) {
		struct trident_channel *channel = &bank->channels[idx];
		bank->bitmap |= 1 << idx;
		channel->num = idx;
		return channel;
	}
	return NULL;
}


static void trident_free_pcm_channel(struct trident_card *card, int channel)
{
	int bank;

	if (channel < 31 || channel > 63)
		return;

	bank = channel >> 5;
	channel = channel & 0x1f;

	card->banks[bank].bitmap &= ~(1 << (channel));
}

static void ali_free_pcm_channel(struct trident_card *card, int channel)
{
	int bank;

	if (channel > 31)
		return;

	bank = channel >> 5;
	channel = channel & 0x1f;

	card->banks[bank].bitmap &= ~(1 << (channel));
}


/* called with spin lock held */

static int trident_load_channel_registers(struct trident_card *card, u32 *data, unsigned int channel)
{
	int i;

	if (channel > 63)
		return FALSE;

	/* select hardware channel to write */
	outb(channel, TRID_REG(card, T4D_LFO_GC_CIR));

	/* Output the channel registers, but don't write register
	   three to an ALI chip. */
	for (i = 0; i < CHANNEL_REGS; i++) {
		if (i == 3 && card->pci_id == PCI_DEVICE_ID_ALI_5451)
			continue;
		outl(data[i], TRID_REG(card, CHANNEL_START + 4*i));
	}
	return TRUE;
}

/* called with spin lock held */
static int trident_write_voice_regs(struct trident_state *state)
{
	u32 data[CHANNEL_REGS + 1];
	struct trident_channel *channel;

	channel = state->dmabuf.channel;

	data[1] = channel->lba;
	data[4] = channel->control;

	switch (state->card->pci_id)
	{
	case PCI_DEVICE_ID_ALI_5451:
		data[0] = 0; /* Current Sample Offset */
		data[2] = (channel->eso << 16) | (channel->delta & 0xffff);
		data[3] = 0;
		break;	
	case PCI_DEVICE_ID_SI_7018:
		data[0] = 0; /* Current Sample Offset */
		data[2] = (channel->eso << 16) | (channel->delta & 0xffff);
		data[3] = (channel->attribute << 16) | (channel->fm_vol & 0xffff);
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		data[0] = 0; /* Current Sample Offset */
		data[2] = (channel->eso << 16) | (channel->delta & 0xffff);
		data[3] = channel->fm_vol & 0xffff;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		data[0] = (channel->delta << 24);
		data[2] = ((channel->delta << 16) & 0xff000000) | (channel->eso & 0x00ffffff);
		data[3] = channel->fm_vol & 0xffff;
		break;
	default:
		return FALSE;
	}

	return trident_load_channel_registers(state->card, data, channel->num);
}

static int compute_rate_play(u32 rate)
{
	int delta;
	/* We special case 44100 and 8000 since rounding with the equation
	   does not give us an accurate enough value. For 11025 and 22050
	   the equation gives us the best answer. All other frequencies will
	   also use the equation. JDW */
	if (rate == 44100)
		delta = 0xeb3;
	else if (rate == 8000)
		delta = 0x2ab;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = (((rate << 12) + rate) / 48000) & 0x0000ffff;
	return delta;
}

static int compute_rate_rec(u32 rate)
{
	int delta;

	if (rate == 44100)
		delta = 0x116a;
	else if (rate == 8000)
		delta = 0x6000;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = ((48000 << 12) / rate) & 0x0000ffff;

	return delta;
}
/* set playback sample rate */
static unsigned int trident_set_dac_rate(struct trident_state * state, unsigned int rate)
{	
	struct dmabuf *dmabuf = &state->dmabuf;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	dmabuf->rate = rate;
	dmabuf->channel->delta = compute_rate_play(rate);

	trident_write_voice_regs(state);

#ifdef DEBUG
	printk("trident: called trident_set_dac_rate : rate = %d\n", rate);
#endif

	return rate;
}

/* set recording sample rate */
#if 0
static unsigned int trident_set_adc_rate(struct trident_state * state, unsigned int rate)
{
	struct dmabuf *dmabuf = &state->dmabuf;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	dmabuf->rate = rate;
	dmabuf->channel->delta = compute_rate_rec(rate);

	trident_write_voice_regs(state);

#ifdef DEBUG
	printk("trident: called trident_set_adc_rate : rate = %d\n", rate);
#endif
	return rate;
}
#endif

/* prepare channel attributes for playback */ 
static void trident_play_setup(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct trident_channel *channel = dmabuf->channel;

	channel->lba = virt_to_bus(dmabuf->rawbuf);
	channel->delta = compute_rate_play(dmabuf->rate);

	channel->eso = dmabuf->dmasize >> sample_shift[dmabuf->fmt];
	channel->eso -= 1;

	if (state->card->pci_id != PCI_DEVICE_ID_SI_7018) {
		channel->attribute = 0;
	}

	channel->fm_vol = 0x0;
	
	channel->control = CHANNEL_LOOP;
	if (dmabuf->fmt & TRIDENT_FMT_16BIT) {
		/* 16-bits */
		channel->control |= CHANNEL_16BITS;
		/* signed */
		channel->control |= CHANNEL_SIGNED;
	}
	if (dmabuf->fmt & TRIDENT_FMT_STEREO)
		/* stereo */
		channel->control |= CHANNEL_STEREO;
#ifdef DEBUG
	printk("trident: trident_play_setup, LBA = 0x%08x, "
	       "Delta = 0x%08x, ESO = 0x%08x, Control = 0x%08x\n",
	       channel->lba, channel->delta, channel->eso, channel->control);
#endif
	trident_write_voice_regs(state);
}

/* prepare channel attributes for recording */
static void trident_rec_setup(struct trident_state *state)
{
	u16 w;
	struct trident_card *card = state->card;
	struct dmabuf *dmabuf = &state->dmabuf;
	struct trident_channel *channel = dmabuf->channel;

	/* Enable AC-97 ADC (capture) */
	switch (card->pci_id) 
	{
	case PCI_DEVICE_ID_ALI_5451:
	case PCI_DEVICE_ID_SI_7018:
		/* for 7018, the ac97 is always in playback/record (duplex) mode */
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		w = inb(TRID_REG(card, DX_ACR2_AC97_COM_STAT));
		outb(w | 0x48, TRID_REG(card, DX_ACR2_AC97_COM_STAT));
		/* enable and set record channel */
		outb(0x80 | channel->num, TRID_REG(card, T4D_REC_CH));
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		w = inw(TRID_REG(card, T4D_MISCINT));
		outw(w | 0x1000, TRID_REG(card, T4D_MISCINT));
		/* enable and set record channel */
		outb(0x80 | channel->num, TRID_REG(card, T4D_REC_CH));
		break;
	default:
		return;
	}

	channel->lba = virt_to_bus(dmabuf->rawbuf);
	channel->delta = compute_rate_rec(dmabuf->rate);

	channel->eso = dmabuf->dmasize >> sample_shift[dmabuf->fmt];
	channel->eso -= 1;

	if (state->card->pci_id != PCI_DEVICE_ID_SI_7018) {
		channel->attribute = 0;
	}

	channel->fm_vol = 0x0;
	
	channel->control = CHANNEL_LOOP;
	if (dmabuf->fmt & TRIDENT_FMT_16BIT) {
		/* 16-bits */
		channel->control |= CHANNEL_16BITS;
		/* signed */
		channel->control |= CHANNEL_SIGNED;
	}
	if (dmabuf->fmt & TRIDENT_FMT_STEREO)
		/* stereo */
		channel->control |= CHANNEL_STEREO;
#ifdef DEBUG
	printk("trident: trident_rec_setup, LBA = 0x%08x, "
	       "Delat = 0x%08x, ESO = 0x%08x, Control = 0x%08x\n",
	       channel->lba, channel->delta, channel->eso, channel->control);
#endif
	trident_write_voice_regs(state);
}

/* get current playback/recording dma buffer pointer (byte offset from LBA),
   called with spinlock held! */
extern __inline__ unsigned trident_get_dma_addr(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	u32 cso;

	if (!dmabuf->enable)
		return 0;

	outb(dmabuf->channel->num, TRID_REG(state->card, T4D_LFO_GC_CIR));

	switch (state->card->pci_id) 
	{
	case PCI_DEVICE_ID_ALI_5451:
	case PCI_DEVICE_ID_SI_7018:
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		/* 16 bits ESO, CSO for 7018 and DX */
		cso = inw(TRID_REG(state->card, CH_DX_CSO_ALPHA_FMS + 2));
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		/* 24 bits ESO, CSO for NX */
		cso = inl(TRID_REG(state->card, CH_NX_DELTA_CSO)) & 0x00ffffff;
		break;
	default:
		return 0;
	}

#ifdef DEBUG
	printk("trident: trident_get_dma_addr: chip reported channel: %d, "
	       "cso = 0x%04x\n",
	       dmabuf->channel->num, cso);
#endif
	/* ESO and CSO are in units of Samples, convert to byte offset */
	cso <<= sample_shift[dmabuf->fmt];

	return (cso % dmabuf->dmasize);
}

/* Stop recording (lock held) */
extern __inline__ void __stop_adc(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;

	dmabuf->enable &= ~ADC_RUNNING;
	trident_stop_voice(card, chan_num);
	trident_disable_voice_irq(card, chan_num);
}

static void start_adc(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;
	unsigned int flags;

	spin_lock_irqsave(&card->lock, flags);
	if ((dmabuf->mapped || dmabuf->count < (signed)dmabuf->dmasize) && dmabuf->ready) {
		dmabuf->enable |= ADC_RUNNING;
		trident_enable_voice_irq(card, chan_num);
		trident_start_voice(card, chan_num);
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

/* stop playback (lock held) */
extern __inline__ void __stop_dac(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;

	dmabuf->enable &= ~DAC_RUNNING;
	trident_stop_voice(card, chan_num);
	trident_disable_voice_irq(card, chan_num);
}

static void stop_dac(struct trident_state *state)
{
	struct trident_card *card = state->card;
	unsigned int flags;

	spin_lock_irqsave(&card->lock, flags);
	__stop_dac(state);
	spin_unlock_irqrestore(&card->lock, flags);
}	

static void start_dac(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;
	unsigned int flags;

	spin_lock_irqsave(&card->lock, flags);
	if ((dmabuf->mapped || dmabuf->count > 0) && dmabuf->ready) {
		dmabuf->enable |= DAC_RUNNING;
		trident_enable_voice_irq(card, chan_num);
		trident_start_voice(card, chan_num);
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

#define DMABUF_DEFAULTORDER (15-PAGE_SHIFT)
#define DMABUF_MINORDER 1

/* allocate DMA buffer, playback and recording buffer should be allocated seperately */
static int alloc_dmabuf(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	void *rawbuf;
	int order = 0;

	if (!(rawbuf = pci_alloc_consistent(state->card->pci_dev, PAGE_SIZE << order,
					    &dmabuf->dma_handle)))
		return -ENOMEM;


#ifdef DEBUG
	printk("trident: allocated %ld (order = %d) bytes at %p\n",
	       PAGE_SIZE << order, order, rawbuf);
#endif

	dmabuf->ready  = dmabuf->mapped = 0;
	dmabuf->rawbuf = rawbuf;
	dmabuf->buforder = order;

	return 0;
}

/* free DMA buffer */
static void dealloc_dmabuf(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;

	if (dmabuf->rawbuf) {
		pci_free_consistent(state->card->pci_dev, PAGE_SIZE << dmabuf->buforder,
				    dmabuf->rawbuf, dmabuf->dma_handle);
	}
	dmabuf->rawbuf = NULL;
	dmabuf->mapped = dmabuf->ready = 0;
}

static int prog_dmabuf(struct trident_state *state, unsigned rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned bytepersec;
	unsigned bufsize;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&state->card->lock, flags);
	dmabuf->hwptr = dmabuf->swptr = dmabuf->total_bytes = 0;
	dmabuf->count = dmabuf->error = 0;
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* allocate DMA buffer if not allocated yet */
	if (!dmabuf->rawbuf)
		if ((ret = alloc_dmabuf(state)))
			return ret;

	/* FIXME: figure out all this OSS fragment stuff */
	bytepersec = dmabuf->rate << sample_shift[dmabuf->fmt];
	bufsize = PAGE_SIZE << dmabuf->buforder;
	if (dmabuf->ossfragshift) {
		if ((1000 << dmabuf->ossfragshift) < bytepersec)
			dmabuf->fragshift = ld2(bytepersec/1000);
		else
			dmabuf->fragshift = dmabuf->ossfragshift;
	} else {
		/* lets hand out reasonable big ass buffers by default */
		dmabuf->fragshift = (dmabuf->buforder + PAGE_SHIFT -2);
	}
	dmabuf->numfrag = bufsize >> dmabuf->fragshift;
	while (dmabuf->numfrag < 4 && dmabuf->fragshift > 3) {
		dmabuf->fragshift--;
		dmabuf->numfrag = bufsize >> dmabuf->fragshift;
	}
	dmabuf->fragsize = 1 << dmabuf->fragshift;
	if (dmabuf->ossmaxfrags >= 4 && dmabuf->ossmaxfrags < dmabuf->numfrag)
		dmabuf->numfrag = dmabuf->ossmaxfrags;
	dmabuf->fragsamples = dmabuf->fragsize >> sample_shift[dmabuf->fmt];
	dmabuf->dmasize = dmabuf->numfrag << dmabuf->fragshift;

	memset(dmabuf->rawbuf, (dmabuf->fmt & TRIDENT_FMT_16BIT) ? 0 : 0x80,
	       dmabuf->dmasize);
	spin_lock_irqsave(&state->card->lock, flags);
	if (rec) {
		trident_rec_setup(state);
	} else {
		trident_play_setup(state);
	}
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* set the ready flag for the dma buffer */
	dmabuf->ready = 1;

#ifdef DEBUG
	printk("trident: prog_dmabuf, sample rate = %d, format = %d, numfrag = %d, "
	       "fragsize = %d dmasize = %d\n",
	       dmabuf->rate, dmabuf->fmt, dmabuf->numfrag,
	       dmabuf->fragsize, dmabuf->dmasize);
#endif

	return 0;
}

/* we are doing quantum mechanics here, the buffer can only be empty, half or full filled i.e.
   |------------|------------|   or   |xxxxxxxxxxxx|------------|   or   |xxxxxxxxxxxx|xxxxxxxxxxxx|
   but we almost always get this
   |xxxxxx------|------------|   or   |xxxxxxxxxxxx|xxxxx-------|
   so we have to clear the tail space to "silence"
   |xxxxxx000000|------------|   or   |xxxxxxxxxxxx|xxxxxx000000|
*/
static void trident_clear_tail(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned swptr;
	unsigned char silence = (dmabuf->fmt & TRIDENT_FMT_16BIT) ? 0 : 0x80;
	unsigned int len;
	unsigned long flags;

	spin_lock_irqsave(&state->card->lock, flags);
	swptr = dmabuf->swptr;
	spin_unlock_irqrestore(&state->card->lock, flags);

	if (swptr == 0 || swptr == dmabuf->dmasize / 2 || swptr == dmabuf->dmasize)
		return;

	if (swptr < dmabuf->dmasize/2)
		len = dmabuf->dmasize/2 - swptr;
	else
		len = dmabuf->dmasize - swptr;

	memset(dmabuf->rawbuf + swptr, silence, len);

	spin_lock_irqsave(&state->card->lock, flags);
	dmabuf->swptr += len;
	dmabuf->count += len;
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* restart the dma machine in case it is halted */
	start_dac(state);
}

static int drain_dac(struct trident_state *state, int nonblock)
{
	//struct wait_queue wait = {current, NULL};
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned long tmo;
	int count;

	if (dmabuf->mapped || !dmabuf->ready)
		return 0;

	for (;;) {
		spin_lock_irqsave(&state->card->lock, flags);
		count = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (count <= 0)
			break;

		/* No matter how much data left in the buffer, we have to wait untill
		   CSO == ESO/2 or CSO == ESO when address engine interrupts */
		tmo = (dmabuf->dmasize * HZ) / dmabuf->rate;
		tmo >>= sample_shift[dmabuf->fmt];
	}

	return 0;
}

/* update buffer manangement pointers, especially, dmabuf->count and dmabuf->hwptr */
static void trident_update_ptr(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned hwptr, swptr;
	int clear_cnt = 0;
	int diff;
	unsigned char silence;
	unsigned half_dmasize;

	/* update hardware pointer */
	hwptr = trident_get_dma_addr(state);
	diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;
	dmabuf->hwptr = hwptr;
	dmabuf->total_bytes += diff;

	/* error handling and process wake up for ADC */
	if (dmabuf->enable == ADC_RUNNING) {
		if (dmabuf->mapped) {
			dmabuf->count -= diff;
			if (dmabuf->count >= (signed)dmabuf->fragsize)
				wakeup_sem(dmabuf->wait, true);
		} else {
			dmabuf->count += diff;

			if (dmabuf->count < 0 || dmabuf->count > dmabuf->dmasize) {
				/* buffer underrun or buffer overrun, we have no way to recover
				   it here, just stop the machine and let the process force hwptr
				   and swptr to sync */
				__stop_adc(state);
				dmabuf->error++;
			}
			if (dmabuf->count < (signed)dmabuf->dmasize/2)
				wakeup_sem(dmabuf->wait, true);
		}
	}

	/* error handling and process wake up for DAC */
	if (dmabuf->enable == DAC_RUNNING) {
		if (dmabuf->mapped) {
			dmabuf->count += diff;
			if (dmabuf->count >= (signed)dmabuf->fragsize)
				wakeup_sem(dmabuf->wait, true);
		} else {
			dmabuf->count -= diff;

			if (dmabuf->count < 0 || dmabuf->count > dmabuf->dmasize) {
				/* buffer underrun or buffer overrun, we have no way to recover
				   it here, just stop the machine and let the process force hwptr
				   and swptr to sync */
				__stop_dac(state);
				dmabuf->error++;
			}
			else if (!dmabuf->endcleared) {
				swptr = dmabuf->swptr;
				silence = (dmabuf->fmt & TRIDENT_FMT_16BIT ? 0 : 0x80);
				if (dmabuf->update_flag & ALI_ADDRESS_INT_UPDATE) {
					/* We must clear end data of 1/2 dmabuf if needed.
					   According to 1/2 algorithm of Address Engine Interrupt,
					   check the validation of the data of half dmasize. */
					half_dmasize = dmabuf->dmasize / 2;
					if ((diff = hwptr - half_dmasize) < 0 )
						diff = hwptr;
					if ((dmabuf->count + diff) < half_dmasize) {
						//there is invalid data in the end of half buffer
						if ((clear_cnt = half_dmasize - swptr) < 0)
							clear_cnt += half_dmasize;
						//clear the invalid data
						memset (dmabuf->rawbuf + swptr,
							silence, clear_cnt);
						dmabuf->endcleared = 1;
					}
				} else if (dmabuf->count < (signed) dmabuf->fragsize) {
					clear_cnt = dmabuf->fragsize;
					if ((swptr + clear_cnt) > dmabuf->dmasize)
						clear_cnt = dmabuf->dmasize - swptr;
					memset (dmabuf->rawbuf + swptr, silence, clear_cnt);
					dmabuf->endcleared = 1;
				}
			}
			/* trident_update_ptr is called by interrupt handler or by process via
			   ioctl/poll, we only wake up the waiting process when we have more
			   than 1/2 buffer free (always true for interrupt handler) */
			if (dmabuf->count < (signed)dmabuf->dmasize/2)
				wakeup_sem(dmabuf->wait, true);
		}
	}
	dmabuf->update_flag &= ~ALI_ADDRESS_INT_UPDATE;
}

static void trident_address_interrupt(struct trident_card *card)
{
	int i;
	struct trident_state *state;
	
	/* Update the pointers for all channels we are running. */
	/* FIXME: should read interrupt status only once */
	for (i = 0; i < NR_HW_CH; i++) {
		if (trident_check_channel_interrupt(card, 63 - i)) {
			trident_ack_channel_interrupt(card, 63 - i);
			if ((state = card->states[i]) != NULL) {
				trident_update_ptr(state);
			} else {
				printk("trident: spurious channel irq %d.\n",
				       63 - i);
				trident_stop_voice(card, 63 - i);
				trident_disable_voice_irq(card, 63 - i);
			}
		}
	}
}

static void ali_address_interrupt(struct trident_card *card)
{
	int i;
	u32 mask = trident_get_interrupt_mask (card, BANK_A);

#ifdef DEBUG
	/* Sanity check to make sure that every state has a channel
	   and vice versa. */
	u32 done = 0;
	unsigned ns = 0;
	unsigned nc = 0;
	for (i = 0; i < NR_HW_CH; i++) {
		if (card->banks[BANK_A].bitmap & (1<<i))
			nc ++;

		if (card->states[i]) {
			u32 bit = 1 << card->states[i]->dmabuf.channel->num;
			if (bit & done) 
				printk (KERN_ERR "trident: channel allocated to two states\n");
			ns++;

			done |= bit;
		}
	}
	if (ns != nc) 
		printk (KERN_ERR "trident: number of states != number of channels\n");
#endif

	for (i = 0; mask && i < NR_HW_CH; i++) {			
		struct trident_state *state = card->states[i];
		if (!state)
			continue;

		trident_ack_channel_interrupt(card, state->dmabuf.channel->num);
		mask &= ~ (1<<state->dmabuf.channel->num);
		state->dmabuf.update_flag |= ALI_ADDRESS_INT_UPDATE;
		trident_update_ptr(state);
	}

	if (mask) 
		for (i = 0; i < NR_HW_CH; i++)
			if (mask & (1<<i)) {
				printk("ali: spurious channel irq %d.\n", i);
				trident_stop_voice(card, i);
				trident_disable_voice_irq(card, i);
			}

}

static int trident_interrupt(int irq, void *dev_id, SysCallRegs_s *regs)
{
	struct trident_card *card = (struct trident_card *)dev_id;
	u32 event;

	spin_lock(&card->lock);
	event = inl(TRID_REG(card, T4D_MISCINT));

#ifdef DEBUG
	printk("trident: trident_interrupt called, MISCINT = 0x%08x\n", event);
#endif

	if (event & ADDRESS_IRQ) {
		card->address_interrupt(card);
	}
	
	/* manually clear interrupt status, bad hardware design, blame T^2 */
	outl((ST_TARGET_REACHED | MIXER_OVERFLOW | MIXER_UNDERFLOW),
	     TRID_REG(card, T4D_MISCINT));
	spin_unlock(&card->lock);
	return 0;
}

/* AtheOS additions */
DeviceOperations_s trident_mixer_fops = {
		trident_mixer_open,
		trident_mixer_release,
		trident_mixer_ioctl,
		NULL,
		NULL
};

DeviceOperations_s trident_dsp_fops = {
		trident_dsp_open,
		trident_dsp_release,
		trident_dsp_ioctl,
		trident_dsp_read,
		trident_dsp_write
};

/* in this loop, dmabuf.count signifies the amount of data that is waiting to be copied to
   the user's buffer.  it is filled by the dma machine and drained by this loop. */
static int trident_dsp_read(void *node, void *cookie, off_t ppos, void *buffer, size_t count)
{
	struct trident_state *state = (struct trident_state *)trident_state_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

#ifdef DEBUG
	printk("trident: trident_read called, count = %d\n", count);
#endif

	VALIDATE_STATE(state);
	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 1)))
		return ret;
	ret = 0;

	if (state->card->pci_id == PCI_DEVICE_ID_ALI_5451)
		outl(inl(TRID_REG (state->card, ALI_GLOBAL_CONTROL)) | ALI_PCM_IN_ENABLE,
		     TRID_REG (state->card, ALI_GLOBAL_CONTROL));

	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
		if (dmabuf->count > (signed) dmabuf->dmasize) {
			/* buffer overrun, we are recovering from sleep_on_timeout,
			   resync hwptr and swptr, make process flush the buffer */
			dmabuf->count = dmabuf->dmasize;
			dmabuf->swptr = dmabuf->hwptr;
		}
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count < cnt)
			cnt = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			unsigned long tmo;
			/* buffer is empty, start the dma machine and wait for data to be
			   recorded */
			start_adc(state);

			/* No matter how much space left in the buffer, we have to wait untill
			   CSO == ESO/2 or CSO == ESO when address engine interrupts */
			tmo = (dmabuf->dmasize * HZ) / (dmabuf->rate * 2);
			tmo >>= sample_shift[dmabuf->fmt];
			/* There are two situations when sleep_on_timeout returns, one is when
			   the interrupt is serviced correctly and the process is waked up by
			   ISR ON TIME. Another is when timeout is expired, which means that
			   either interrupt is NOT serviced correctly (pending interrupt) or it
			   is TOO LATE for the process to be scheduled to run (scheduler latency)
			   which results in a (potential) buffer overrun. And worse, there is
			   NOTHING we can do to prevent it. */
			if (!sleep_on_sem(dmabuf->wait, tmo)) {
#ifdef DEBUG
				printk(KERN_ERR "trident: recording schedule timeout, "
				       "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       dmabuf->dmasize, dmabuf->fragsize, dmabuf->count,
				       dmabuf->hwptr, dmabuf->swptr);
#endif
				/* a buffer overrun, we delay the recovery untill next time the
				   while loop begin and we REALLY have space to record */
			}
			continue;
		}

		if (copy_to_user(buffer, dmabuf->rawbuf + swptr, cnt)) {
			if (!ret) ret = -EFAULT;
			return ret;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&state->card->lock, flags);
		dmabuf->swptr = swptr;
		dmabuf->count -= cnt;
		spin_unlock_irqrestore(&state->card->lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_adc(state);
	}
	return ret;
}

/* in this loop, dmabuf.count signifies the amount of data that is waiting to be dma to
   the soundcard.  it is drained by the dma machine and filled by this loop. */
static int trident_dsp_write(void *node, void *cookie, off_t ppos, const void *buffer, size_t count)
{
	struct trident_state *state = (struct trident_state *) trident_state_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

#ifdef DEBUG
	printk("trident: trident_write called, count = %d\n", count);
#endif

	VALIDATE_STATE(state);
	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
		return ret;
	ret = 0;

	if (state->card->pci_id == PCI_DEVICE_ID_ALI_5451)
		if (dmabuf->channel->num == ALI_PCM_IN_CHANNEL)
			outl ( inl (TRID_REG (state->card, ALI_GLOBAL_CONTROL)) &
			       ALI_PCM_IN_DISABLE, TRID_REG (state->card, ALI_GLOBAL_CONTROL));

	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
		if (dmabuf->count < 0) {
			/* buffer underrun, we are recovering from sleep_on_timeout,
			   resync hwptr and swptr */
			dmabuf->count = 0;
			dmabuf->swptr = dmabuf->hwptr;
		}
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count + cnt > dmabuf->dmasize)
			cnt = dmabuf->dmasize - dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			unsigned long tmo;
			/* buffer is full, start the dma machine and wait for data to be
			   played */
			start_dac(state);
			//if (file->f_flags & O_NONBLOCK) {
			//if (!ret) ret = -EAGAIN;
			//	return ret;
			//}
			/* No matter how much data left in the buffer, we have to wait untill
			   CSO == ESO/2 or CSO == ESO when address engine interrupts */
			tmo = (dmabuf->dmasize * HZ) / (dmabuf->rate * 2);
			tmo >>= sample_shift[dmabuf->fmt];
			/* There are two situations when sleep_on_timeout returns, one is when
			   the interrupt is serviced correctly and the process is waked up by
			   ISR ON TIME. Another is when timeout is expired, which means that
			   either interrupt is NOT serviced correctly (pending interrupt) or it
			   is TOO LATE for the process to be scheduled to run (scheduler latency)
			   which results in a (potential) buffer underrun. And worse, there is
			   NOTHING we can do to prevent it. */
			if(!sleep_on_sem(dmabuf->wait, tmo)) {
#ifdef DEBUG
				printk(KERN_ERR "trident: playback schedule timeout, "
				       "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       dmabuf->dmasize, dmabuf->fragsize, dmabuf->count,
				       dmabuf->hwptr, dmabuf->swptr);
#endif
				/* a buffer underrun, we delay the recovery untill next time the
				   while loop begin and we REALLY have data to play */
			}
			continue;
		}
		if (copy_from_user(dmabuf->rawbuf + swptr, buffer, cnt)) {
			if (!ret) ret = -EFAULT;
			return ret;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&state->card->lock, flags);
		dmabuf->swptr = swptr;
		dmabuf->count += cnt;
		dmabuf->endcleared = 0;
		spin_unlock_irqrestore(&state->card->lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(state);
	}
	return ret;
}

int trident_dsp_ioctl(void *node, void *cookie, uint32 cmd, void *args, bool b_fromkernel)
{
	struct trident_state *state = (struct trident_state *) trident_state_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned long arg = (unsigned long)args;
	audio_buf_info abinfo;
	count_info cinfo;
	int val, ret;

	VALIDATE_STATE(state);
#ifdef DEBUG
	printk("trident: trident_ioctl, command = %2d, arg = 0x%08x\n",
	       _IOC_NR(cmd), arg ? *(int *)arg : 0);
#endif

	switch (cmd) 
	{
	case OSS_GETVERSION:
		put_user(SOUND_VERSION, (int *)arg);
		return 0;

	case SNDCTL_DSP_RESET:
		/* FIXME: spin_lock ? */
		stop_dac(state);
		dmabuf->ready = 0;
		dmabuf->swptr = dmabuf->hwptr = 0;
		dmabuf->count = dmabuf->total_bytes = 0;
		return 0;

	case SNDCTL_DSP_SYNC:
		return drain_dac(state, O_WRONLY & O_NONBLOCK);

	case SNDCTL_DSP_SPEED: /* set sample rate */
		get_user(val, (int *)arg);
		if (val >= 0) {
			stop_dac(state);
			dmabuf->ready = 0;
			spin_lock_irqsave(&state->card->lock, flags);
			trident_set_dac_rate(state, val);
			spin_unlock_irqrestore(&state->card->lock, flags);
		}
		put_user(dmabuf->rate, (int *)arg);
		return 0;

	case SNDCTL_DSP_STEREO: /* set stereo or mono channel */
		get_user(val, (int *)arg);
		stop_dac(state);
		dmabuf->ready = 0;
		if (val)
			dmabuf->fmt |= TRIDENT_FMT_STEREO;
		else
			dmabuf->fmt &= ~TRIDENT_FMT_STEREO;
		return 0;

	case SNDCTL_DSP_GETBLKSIZE:
		if ((val = prog_dmabuf(state, 0)))
			return val;
		put_user(dmabuf->fragsize, (int *)arg);
		return 0;

	case SNDCTL_DSP_GETFMTS: /* Returns a mask of supported sample format*/
		put_user(AFMT_S16_LE|AFMT_U16_LE|AFMT_S8|AFMT_U8, (int *)arg);
		return 0;

	case SNDCTL_DSP_SETFMT: /* Select sample format */
		get_user(val, (int *)arg);
		if (val != AFMT_QUERY) {
			stop_dac(state);
			dmabuf->ready = 0;
			if (val == AFMT_S16_LE)
				dmabuf->fmt |= TRIDENT_FMT_16BIT;
			else
				dmabuf->fmt &= ~TRIDENT_FMT_16BIT;
		}
		put_user((dmabuf->fmt & TRIDENT_FMT_16BIT) ?
				AFMT_S16_LE : AFMT_U8, (int *)arg);
		return 0;

	case SNDCTL_DSP_CHANNELS:
		get_user(val, (int *)arg);
		if (val != 0) {
			stop_dac(state);
			dmabuf->ready = 0;
			if (val >= 2)
				dmabuf->fmt |= TRIDENT_FMT_STEREO;
			else
				dmabuf->fmt &= ~TRIDENT_FMT_STEREO;
		}
		put_user((dmabuf->fmt & TRIDENT_FMT_STEREO) ? 2 : 1,
				(int *)arg);
		return 0;

	case SNDCTL_DSP_POST:
		/* FIXME: the same as RESET ?? */
		return 0;

	case SNDCTL_DSP_SUBDIVIDE:
		if (dmabuf->subdivision)
			return -EINVAL;
		get_user(val, (int *)arg);
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		dmabuf->subdivision = val;
		return 0;

	case SNDCTL_DSP_SETFRAGMENT:
		get_user(val, (int *)arg);

		dmabuf->ossfragshift = val & 0xffff;
		dmabuf->ossmaxfrags = (val >> 16) & 0xffff;
		if (dmabuf->ossfragshift < 4)
			dmabuf->ossfragshift = 4;
		if (dmabuf->ossfragshift > 15)
			dmabuf->ossfragshift = 15;
		if (dmabuf->ossmaxfrags < 4)
			dmabuf->ossmaxfrags = 4;

		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!dmabuf->enable && (val = prog_dmabuf(state, 0)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		abinfo.fragsize = dmabuf->fragsize;
		abinfo.bytes = dmabuf->dmasize - dmabuf->count;
		abinfo.fragstotal = dmabuf->numfrag;
		abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_NONBLOCK:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		put_user(DSP_CAP_REALTIME|DSP_CAP_TRIGGER|DSP_CAP_MMAP, 
			    (int *)arg);
		return 0;

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		val |= PCM_ENABLE_OUTPUT;
		put_user(val, (int *)arg);
		return 0;

	case SNDCTL_DSP_SETTRIGGER:
		get_user(val, (int *)arg);
		if (val & PCM_ENABLE_OUTPUT) {
			if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
				return ret;
			start_dac(state);
		} else
			stop_dac(state);
		return 0;

	case SNDCTL_DSP_GETOPTR:
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = dmabuf->count >> dmabuf->fragshift;
		cinfo.ptr = dmabuf->hwptr;
		if (dmabuf->mapped)
			dmabuf->count &= dmabuf->fragsize-1;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

	case SNDCTL_DSP_SETDUPLEX:
		return -EINVAL;

	case SNDCTL_DSP_GETODELAY:
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		val = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);
		put_user(val, (int *)arg);
		return 0;

	case SOUND_PCM_READ_RATE:
		put_user(dmabuf->rate, (int *)arg);
		return 0;

	case SOUND_PCM_READ_CHANNELS:
		put_user((dmabuf->fmt & TRIDENT_FMT_STEREO) ? 2 : 1,
				(int *)arg);
		return 0;

	case SOUND_PCM_READ_BITS:
		put_user((dmabuf->fmt & TRIDENT_FMT_16BIT) ?
				AFMT_S16_LE : AFMT_U8, (int *)arg);
		return 0;

	case IOCTL_GET_USERSPACE_DRIVER:
	{
		memcpy_to_user( args, "oss.so", strlen( "oss.so" ) );
		return( 0 );
	}

	case SNDCTL_DSP_GETISPACE:
	case SNDCTL_DSP_GETIPTR:
	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
		
	}
	return -EINVAL;
}

static status_t trident_dsp_open (void* pNode, uint32 nFlags, void **pCookie )
{
	int i = 0;
	struct trident_card *card = devs;
	struct trident_state *state = NULL;
	struct dmabuf *dmabuf = NULL;

	/* find an avaiable virtual channel (instance of /dev/dsp) */
	while (card != NULL) {
		down(card->open_sem);
		for (i = 0; i < NR_HW_CH; i++) {
			if (card->states[i] == NULL) {
				state = card->states[i] = (struct trident_state *)
					kmalloc(sizeof(struct trident_state), MEMF_KERNEL);
				if (state == NULL) {
					return -ENOMEM;
				}
				memset(state, 0, sizeof(struct trident_state));
				dmabuf = &state->dmabuf;
				goto found_virt;
			}
		}
		up(card->open_sem);
		card = card->next;
	}
	/* no more virtual channel avaiable */
	if (!state) {
		return -ENODEV;
	}
 found_virt:
	/* found a free virtual channel, allocate hardware channels */
	dmabuf->channel = card->alloc_pcm_channel(card);
		
	if (dmabuf->channel == NULL) {
		kfree (card->states[i]);
		card->states[i] = NULL;
		return -ENODEV;
	}

	/* initialize the virtual channel */
	state->virt = i;
	state->card = card;
	state->magic = TRIDENT_STATE_MAGIC;
	dmabuf->wait = create_semaphore("trident_wait", 3, 0);
	trident_state_data = state;

	/* set default sample format. According to OSS Programmer's Guide  /dev/dsp
	   should be default to unsigned 8-bits, mono, with sample rate 8kHz and
	   /dev/dspW will accept 16-bits sample */
	dmabuf->fmt &= ~TRIDENT_FMT_MASK;
	dmabuf->fmt |= TRIDENT_FMT_16BIT | TRIDENT_FMT_STEREO; /* For AtheOS */
	dmabuf->ossfragshift = 0;
	dmabuf->ossmaxfrags  = 0;
	dmabuf->subdivision  = 0;
	if (card->pci_id == PCI_DEVICE_ID_SI_7018) {
		/* set default channel attribute to normal playback */
		dmabuf->channel->attribute = CHANNEL_PB;
	}
	trident_set_dac_rate(state, 44100); /* default for AtheOS */

	state->open_mode |= FMODE_WRITE;
	up(card->open_sem);

#ifdef DEBUG
       printk(KERN_ERR "trident: open virtual channel %d, hard channel %d\n", 
              state->virt, dmabuf->channel->num);
#endif

	return 0;
}

static status_t trident_dsp_release(void *pNode, void *pCookie)
{
	struct trident_state *state = (struct trident_state *) trident_state_data;
	struct trident_card *card = state->card;
	struct dmabuf *dmabuf = &state->dmabuf;

	VALIDATE_STATE(state);

	trident_clear_tail(state);
	drain_dac(state, O_WRONLY & O_NONBLOCK);

	/* stop DMA state machine and free DMA buffers/channels */
	down(card->open_sem);

	stop_dac(state);
	dealloc_dmabuf(state);
	state->card->free_pcm_channel(state->card, dmabuf->channel->num);

	card->states[state->virt] = NULL;
	kfree(state);

	/* we're covered by the open_sem */
	up(card->open_sem);

	return 0;
}

/* AtheOS: MIDI & FM removed */

/* trident specific AC97 functions */
/* Write AC97 codec registers */
static void trident_ac97_set(struct ac97_codec *codec, u8 reg, u16 val)
{
	struct trident_card *card = (struct trident_card *)codec->private_data;
	unsigned int address, mask, busy;
	unsigned short count  = 0xffff;
	unsigned long flags;
	u32 data;

	data = ((u32) val) << 16;

	switch (card->pci_id)
	{
	default:
	case PCI_DEVICE_ID_SI_7018:
		address = SI_AC97_WRITE;
		mask = SI_AC97_BUSY_WRITE | SI_AC97_AUDIO_BUSY;
		if (codec->id)
			mask |= SI_AC97_SECONDARY;
		busy = SI_AC97_BUSY_WRITE;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		address = DX_ACR0_AC97_W;
		mask = busy = DX_AC97_BUSY_WRITE;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		address = NX_ACR1_AC97_W;
		mask = NX_AC97_BUSY_WRITE;
		if (codec->id)
			mask |= NX_AC97_WRITE_SECONDARY;
		busy = NX_AC97_BUSY_WRITE;
		break;
	}

	spin_lock_irqsave(&card->lock, flags);
	do {
		if ((inw(TRID_REG(card, address)) & busy) == 0)
			break;
	} while (count--);


	data |= (mask | (reg & AC97_REG_ADDR));

	if (count == 0) {
		printk(KERN_ERR "trident: AC97 CODEC write timed out.\n");
		spin_unlock_irqrestore(&card->lock, flags);
		return;
	}

	outl(data, TRID_REG(card, address));
	spin_unlock_irqrestore(&card->lock, flags);
}

/* Read AC97 codec registers */
static u16 trident_ac97_get(struct ac97_codec *codec, u8 reg)
{
	struct trident_card *card = (struct trident_card *)codec->private_data;
	unsigned int address, mask, busy;
	unsigned short count = 0xffff;
	unsigned long flags;
	u32 data;

	switch (card->pci_id)
	{
	default:
	case PCI_DEVICE_ID_SI_7018:
		address = SI_AC97_READ;
		mask = SI_AC97_BUSY_READ | SI_AC97_AUDIO_BUSY;
		if (codec->id)
			mask |= SI_AC97_SECONDARY;
		busy = SI_AC97_BUSY_READ;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		address = DX_ACR1_AC97_R;
		mask = busy = DX_AC97_BUSY_READ;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		if (codec->id)
			address = NX_ACR3_AC97_R_SECONDARY;
		else
			address = NX_ACR2_AC97_R_PRIMARY;
		mask = NX_AC97_BUSY_READ;
		busy = NX_AC97_BUSY_READ | NX_AC97_BUSY_DATA;
		break;
	}

	data = (mask | (reg & AC97_REG_ADDR));

	spin_lock_irqsave(&card->lock, flags);
	outl(data, TRID_REG(card, address));
	do {
		data = inl(TRID_REG(card, address));
		if ((data & busy) == 0)
			break;
	} while (count--);
	spin_unlock_irqrestore(&card->lock, flags);

	if (count == 0) {
		printk(KERN_ERR "trident: AC97 CODEC read timed out.\n");
		data = 0;
	}
	return ((u16) (data >> 16));
}

/* Write AC97 codec registers for ALi*/
static void ali_ac97_set(struct ac97_codec *codec, u8 reg, u16 val)
{
	struct trident_card *card = (struct trident_card *)codec->private_data;
	unsigned int address, mask;
	unsigned int wCount1 = 0xffff;
	unsigned int wCount2= 0xffff;
	unsigned long chk1, chk2;
	unsigned long flags;
	u32 data;

	data = ((u32) val) << 16;

	address = ALI_AC97_WRITE;
	mask = ALI_AC97_WRITE_ACTION | ALI_AC97_AUDIO_BUSY;
	if (codec->id)
		mask |= ALI_AC97_SECONDARY;
	if (card->revision == 0x02)
		mask |= ALI_AC97_WRITE_MIXER_REGISTER;
		
	spin_lock_irqsave(&card->lock, flags);
	while (wCount1--) {
		if ((inw(TRID_REG(card, address)) & ALI_AC97_BUSY_WRITE) == 0) {
			data |= (mask | (reg & AC97_REG_ADDR));
			
			chk1 = inl(TRID_REG(card,  ALI_STIMER));
			chk2 = inl(TRID_REG(card,  ALI_STIMER));
			while (wCount2-- && (chk1 == chk2))
				chk2 = inl(TRID_REG(card,  ALI_STIMER));
			if (wCount2 == 0) {
				spin_unlock_irqrestore(&card->lock, flags);
				return;
			}
			outl(data, TRID_REG(card, address));	//write!
			spin_unlock_irqrestore(&card->lock, flags);
			return;	//success
		}
		inw(TRID_REG(card, address));	//wait a read cycle
	}

	printk(KERN_ERR "ali: AC97 CODEC write timed out.\n");
	spin_unlock_irqrestore(&card->lock, flags);
	return;
}

/* Read AC97 codec registers for ALi*/
static u16 ali_ac97_get(struct ac97_codec *codec, u8 reg)
{
	struct trident_card *card = (struct trident_card *)codec->private_data;
	unsigned int address, mask;
        unsigned int wCount1 = 0xffff;
        unsigned int wCount2= 0xffff;
        unsigned long chk1, chk2;
	unsigned long flags;
	u32 data;

	address = ALI_AC97_READ;
	if (card->revision == 0x02) {
		address = ALI_AC97_WRITE;
		mask &= ALI_AC97_READ_MIXER_REGISTER;
	}
	mask = ALI_AC97_READ_ACTION | ALI_AC97_AUDIO_BUSY;
	if (codec->id)
		mask |= ALI_AC97_SECONDARY;

	spin_lock_irqsave(&card->lock, flags);
	data = (mask | (reg & AC97_REG_ADDR));
	while (wCount1--) {
		if ((inw(TRID_REG(card, address)) & ALI_AC97_BUSY_READ) == 0) {
			chk1 = inl(TRID_REG(card,  ALI_STIMER));
			chk2 = inl(TRID_REG(card,  ALI_STIMER));
			while (wCount2-- && (chk1 == chk2))
				chk2 = inl(TRID_REG(card,  ALI_STIMER));
			if (wCount2 == 0) {
				printk(KERN_ERR "ali: AC97 CODEC read timed out.\n");
				spin_unlock_irqrestore(&card->lock, flags);
				return 0;
			}
			outl(data, TRID_REG(card, address));	//read!
			wCount2 = 0xffff;
			while (wCount2--) {
				if ((inw(TRID_REG(card, address)) & ALI_AC97_BUSY_READ) == 0) {
					data = inl(TRID_REG(card, address));
					spin_unlock_irqrestore(&card->lock, flags);
					return ((u16) (data >> 16));
				}
			}
		}
		inw(TRID_REG(card, address));	//wait a read cycle
	}
	spin_unlock_irqrestore(&card->lock, flags);
	printk(KERN_ERR "ali: AC97 CODEC read timed out.\n");
	return 0;
}

/* OSS /dev/mixer file operation methods */
static status_t trident_mixer_open(void* pNode, uint32 nFlags, void **pCookie)
{
	int i;
	struct trident_card *card = devs;

	for (card = devs; card != NULL; card = card->next)
		for (i = 0; i < NR_AC97; i++)
			if (card->ac97_codec[i] != NULL)
				goto match;

	if (!card) {
		return -ENODEV;
	}
 match:
	pNode = card->ac97_codec[i];

	return 0;
}

static status_t trident_mixer_release(void *pNode, void *pCookie)
{
	return 0;
}

static status_t trident_mixer_ioctl (void *node, void *cookie, uint32 cmd, void *args, bool b_fromkernel)
{
	unsigned long arg = (unsigned long)args;
	struct ac97_codec *codec = (struct ac97_codec *) node;

	return codec->mixer_ioctl(codec, cmd, arg);
}

/* AC97 codec initialisation. */
static int trident_ac97_init(struct trident_card *card)
{
	int num_ac97 = 0;
	int ready_2nd = 0;
	struct ac97_codec *codec;

	/* initialize controller side of AC link, and find out if secondary codes
	   really exist */
	switch (card->pci_id)
	{
	case PCI_DEVICE_ID_ALI_5451:
		outl(PCMOUT|SECONDARY_ID, TRID_REG(card, SI_SERIAL_INTF_CTRL));
		ready_2nd = inl(TRID_REG(card, SI_SERIAL_INTF_CTRL)); 
		ready_2nd &= SI_AC97_SECONDARY_READY;
		break;
	case PCI_DEVICE_ID_SI_7018:
		/* disable AC97 GPIO interrupt */
		outl(0x00, TRID_REG(card, SI_AC97_GPIO));
		/* when power up the AC link is in cold reset mode so stop it */
		outl(PCMOUT|SURROUT|CENTEROUT|LFEOUT|SECONDARY_ID,
		     TRID_REG(card, SI_SERIAL_INTF_CTRL));
		/* it take a long time to recover from a cold reset (especially when you have
		   more than one codec) */
		udelay(2000);
		ready_2nd = inl(TRID_REG(card, SI_SERIAL_INTF_CTRL));
		ready_2nd &= SI_AC97_SECONDARY_READY;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		/* playback on */
		outl(DX_AC97_PLAYBACK, TRID_REG(card, DX_ACR2_AC97_COM_STAT));
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		/* enable AC97 Output Slot 3,4 (PCM Left/Right Playback) */
		outl(NX_AC97_PCM_OUTPUT, TRID_REG(card, NX_ACR0_AC97_COM_STAT));
		ready_2nd = inl(TRID_REG(card, NX_ACR0_AC97_COM_STAT));
		ready_2nd &= NX_AC97_SECONDARY_READY;
		break;
	}

	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {
               char* devname;
		if ((codec = kmalloc(sizeof(struct ac97_codec), MEMF_KERNEL)) == NULL)
			return -ENOMEM;
		memset(codec, 0, sizeof(struct ac97_codec));

		/* initialize some basic codec information, other fields will be filled
		   in ac97_probe_codec */
		codec->private_data = card;
		codec->id = num_ac97;

		if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
			codec->codec_read = ali_ac97_get;
			codec->codec_write = ali_ac97_set;
		}
		else {
			codec->codec_read = trident_ac97_get;
			codec->codec_write = trident_ac97_set;
		}
	
               if (ac97_probe_codec(codec) == 0) {
                       kfree(codec);
			break;
               }

               if ((devname = kmalloc(30,MEMF_KERNEL)) == NULL) {
                       kfree(codec); return -ENOMEM;
               }
               strcpy(devname,"audio/mixer/trident");
               devname[strlen(devname)-1] = '0'+num_ac97;

               if( create_device_node( card->nDeviceID, card->pci_dev->nHandle, "audio/mixer/trident", &trident_mixer_fops, codec ) < 0 ) {
			printk( "trident: failed to create mixer node \n");
                       kfree(codec); kfree(devname);
			break;
		}

               kfree(devname); /* last component is copied and freed in vfs/dev.c */
		card->ac97_codec[num_ac97] = codec;

		/* if there is no secondary codec at all, don't probe any more */
		if (!ready_2nd)
			return num_ac97+1;
	}

	return num_ac97;
}

/* install the driver, we do not allocate hardware channel nor DMA buffer now, they are defered 
   untill "ACCESS" time (in prog_dmabuf called by open/read/write/ioctl/mmap) */
//static int __init trident_install(struct pci_dev *pci_dev, struct pci_audio_info *pci_info)
static int trident_probe(PCI_Info_s *pci_dev, int pci_id, int nDeviceID, struct pci_audio_info *pci_info)
{
	//u16 w;
	unsigned long iobase;
	struct trident_card *card;
	
	iobase = pci_dev->u.h0.nBase0 & PCI_ADDRESS_MEMORY_32_MASK;

	if ((card = kmalloc(sizeof(struct trident_card), MEMF_KERNEL)) == NULL) {
		printk(KERN_ERR "trident: out of memory\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(*card));

	card->nDeviceID = nDeviceID;
	card->iobase = iobase;
	card->pci_dev = pci_dev;
	card->pci_info = pci_info;
	card->pci_id = pci_dev->nDeviceID;
	card->revision = pci_dev->nRevisionID;
	card->irq = pci_dev->u.h0.nInterruptLine;
	card->next = devs;
	card->magic = TRIDENT_CARD_MAGIC;
	card->banks[BANK_A].addresses = &bank_a_addrs;
	card->banks[BANK_A].bitmap = 0UL;
	card->banks[BANK_B].addresses = &bank_b_addrs;
	card->banks[BANK_B].bitmap = 0UL;

	card->open_sem = create_semaphore("trident_open_sem", 2, 0); 
	spinlock_init(&card->lock, "trident_lock");

	devs = card;
	trident_card_data = card;
	
	printk(KERN_INFO "trident: %s found at IO 0x%04lx, IRQ %d\n",
	       card->pci_info->name, card->iobase, card->irq);
	       
	if( claim_device( nDeviceID, pci_dev->nHandle, card->pci_info->name, DEVICE_AUDIO ) != 0 )
		return( -ENODEV );

	if(card->pci_id == PCI_DEVICE_ID_ALI_5451) {
		card->alloc_pcm_channel = ali_alloc_pcm_channel;
		card->alloc_rec_pcm_channel = ali_alloc_rec_pcm_channel;
		card->free_pcm_channel = ali_free_pcm_channel;
		card->address_interrupt = ali_address_interrupt;
	}
	else {
		card->alloc_pcm_channel = trident_alloc_pcm_channel;
		card->alloc_rec_pcm_channel = trident_alloc_pcm_channel;
		card->free_pcm_channel = trident_free_pcm_channel;
		card->address_interrupt = trident_address_interrupt;
	}

	/* claim our iospace and irq */
	if (request_irq(card->irq, trident_interrupt, NULL, SA_SHIRQ, card->pci_info->name, card) < 0) {
		printk(KERN_ERR "trident: unable to allocate irq %d\n", card->irq);
		kfree(card);
		return -ENODEV;
	}
	/* register /dev/dsp */
	if( create_device_node( nDeviceID, pci_dev->nHandle, "audio/trident", &trident_dsp_fops, trident_card_data ) < 0 ) {
		printk(KERN_ERR "trident: failed to create DSP node \n");
		release_irq(card->irq, 0);
		kfree(card);
		return -ENODEV;
	}
	/* initilize AC97 codec and register /dev/mixer */
	if (trident_ac97_init(card) <= 0) {
		release_irq(card->irq, 0);
		kfree(card);
		return -ENODEV;
	}
	
	outl(0x00, TRID_REG(card, T4D_MUSICVOL_WAVEVOL));

	/* Enable Address Engine Interrupts */
	trident_enable_loop_interrupts(card);

	return 1;
}

status_t device_init(int nDeviceID)
{
	PCI_Info_s *pcidev;
	int foundone = 0;
 	int i;
 
	printk(KERN_INFO "Trident 4DWave/SiS 7018/Ali 5451 PCI Audio, version "
	       DRIVER_VERSION "\n");
 
	for (i = 0; i < sizeof (pci_audio_devices); i++) {
		pcidev = NULL;
		if ((pcidev = pci_find_device(pci_audio_devices[i].vendor,
						 pci_audio_devices[i].device,
						 pcidev)) != NULL) {
			foundone += trident_probe(pcidev, pci_audio_devices[i].device, nDeviceID, pci_audio_devices + i);
 		}
	}
 
	if (!foundone) {
		disable_device( nDeviceID );
		return -ENODEV;
	}
	return 0;
}

static void trident_remove(struct trident_card *card)
{
	int i;

	/* Kill interrupts */
	trident_disable_loop_interrupts(card);

	/* free hardware resources */
	release_irq(card->irq, 0);

	/* unregister audio devices */
	for (i = 0; i < NR_AC97; i++)
		if (card->ac97_codec[i] != NULL) {
			kfree (card->ac97_codec[i]);
		}

	kfree(card);

}

status_t device_uninit( int nDeviceID)
{
	struct trident_card *card = devs;	
	
	/* remove ALL cards */
	while (card != NULL)
	{
		trident_remove (card);
		card = card->next;
	}
	printk("trident: devices successfully unregistered\n");
	return (0);
}
