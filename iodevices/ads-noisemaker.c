/**
 *  ads-noisemaker.c
 *
 *  Emulation of the ADS Noisemaker sound hardware
 *
 *  GI's AY-3-891x once was one of the most common programmable sound
 *  generator chips (PSGs) releasing the CPU from the burden of creating
 *  sounds by directly generating the signal curves. The AY-3-891x family
 *  and its clone YM2149 were used e.g. on the Mockingboard (Apple II),
 *  the Atari ST, the ZX Spectrum and many others.
 *
 *  The ADS Noisemake probably had been the first AY-3-891x based sound
 *  board for the S100 bus and offered full stereo with six independent
 *  tone channels and two noise generator channels, similar to the
 *  Mockingboard for the Apple II.
 *
 *  AY-3-8910 implementation is based on Peter Sovietov's implementation.
 *  Since the ADS Noisemaker is populated with two AY-3-8910 chips for
 *  full stereo playback, the panning code for a single AY-3-8910 has been
 *  removed.
 *  
 *  The I/O functionality of the AY-3-8910 with two parallel ports is
 *  simply ignored and not implemented.
 *  
 *  There is no standard I/O port address known for the AY-3-8910. The few
 *  photos or original hardware which are available suggest that ports 204
 *  to 207 (0xcc to 0xcf) might be a good choice, since they are
 *  in general availabe.
 *  
 *  The ADS Noismaker is mostly a wrapper for the AY-3-8910, providing generic
 *  access to the PSG registers plus stereo amplifier, so just refer to the
 *  AY-3-8910 data sheets for details on programming that PSG.
 *  
 *  Sound implementation needs a real time wave interface, and is based on
 *  the very common PortAudio platform, which is available for most systems,
 *  including Linux, MacOS and Windows.
 *
 *  Noisemaker application with Dazzler:
 *  
 *  Use the 62 Hz vertical blank signal to sync the playback of tunes or
 *  sounds from a score, and write the score to the Noisemaker ports in
 *  a 62 Hz loop. The Noisemaker then does the remaining work.
 *
 *  ADS Noisemaker implementation Copyright (C) 2024 by Ansgar Kueckes
 *
 *  AYUMI code Copyright (c) by Peter Sovietov, http://sovietov.com
 *
 *  PortAudio code Copyright (c) 1999-2006 by Ross Bencina and Phil Burk,
 *  http://www.portaudio.com
 *
 *  z80pack code Copyright (C) 2020 by David McNaughton
 *
 *  History:
 *  10-OCT-2024  Initial release
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sys/time.h"
#include "string.h"
#include "simport.h"

#include "sim.h"

#ifdef HAS_NOISEMAKER

#include "simdefs.h"
#include "simglb.h"

#ifdef HAS_NETSERVER
#include "netsrv.h"
#endif
#include "ads-noisemaker.h"

#ifdef WANT_SDL
#include <SDL.h>
#include "simsdl.h"
#endif

#ifdef WANT_PORTAUDIO
#include "portaudio.h"
#endif

// #define LOG_LOCAL_LEVEL LOG_DEBUG
#include "log.h"
static const char *TAG = "NOISEMAKER";

#define PORT_COUNT  8

static BYTE inPort[PORT_COUNT];
static BYTE outPort[PORT_COUNT];

#ifdef HAS_NETSERVER
static void ads_noisemaker_callback(BYTE *data) {

    int i;
    inPort[0] = *data++;
    for (i=1; i < PORT_COUNT; i++) {
        inPort[i] = (*data++) - 128;
    }
}
#endif

#define DEFAULT_SAMPLE_RATE	44100		/* default SDL audio sample rate in Hz */
#define DEFAULT_RECORDING_LIMIT 10000000	/* size of wave buffer for file output & debug purposes */
#define SAMPLE_BUFFER_SIZE	64		/* audio buffer size per channel, defines audio delay */

/* parameters configurable in system.conf */
long noisemaker_sample_rate = DEFAULT_SAMPLE_RATE;
long noisemaker_recording_limit = DEFAULT_RECORDING_LIMIT;
char *noisemaker_soundfile = NULL;

/* --------------- AY-3-3910 ---------------- */

enum {
  TONE_CHANNELS = 3,
  DECIMATE_FACTOR = 8,
  FIR_SIZE = 192,
  DC_FILTER_SIZE = 1024
};

struct tone_channel {
  int tone_period;
  int tone_counter;
  int tone;
  int t_off;
  int n_off;
  int e_on;
  int volume;
};

struct interpolator {
  double c[4];
  double y[4];
};

struct dc_filter {
  double sum;
  double delay[DC_FILTER_SIZE];
};

struct ayumi {
  struct tone_channel channels[TONE_CHANNELS];
  int noise_period;
  int noise_counter;
  int noise;
  int envelope_counter;
  int envelope_period;
  int envelope_shape;
  int envelope_segment;
  int envelope;
  const double* dac_table;
  double step;
  double x;
  struct interpolator interpolator;
  double fir[FIR_SIZE * 2];
  int fir_index;
  struct dc_filter dc;
  int dc_index;
  double sample;
};

int ayumi_configure(struct ayumi* ay, int is_ym, double clock_rate, int sr);
void ayumi_set_pan(struct ayumi* ay, int index, double pan, int is_eqp);
void ayumi_set_tone(struct ayumi* ay, int index, int period);
void ayumi_set_noise(struct ayumi* ay, int period);
void ayumi_set_mixer(struct ayumi* ay, int index, int t_off, int n_off, int e_on);
void ayumi_set_volume(struct ayumi* ay, int index, int volume);
void ayumi_set_envelope(struct ayumi* ay, int period);
void ayumi_set_envelope_shape(struct ayumi* ay, int shape);
void ayumi_process(struct ayumi* ay);
void ayumi_remove_dc(struct ayumi* ay);

static const double AY_dac_table[] = {
  0.0, 0.0,
  0.00999465934234, 0.00999465934234,
  0.0144502937362, 0.0144502937362,
  0.0210574502174, 0.0210574502174,
  0.0307011520562, 0.0307011520562,
  0.0455481803616, 0.0455481803616,
  0.0644998855573, 0.0644998855573,
  0.107362478065, 0.107362478065,
  0.126588845655, 0.126588845655,
  0.20498970016, 0.20498970016,
  0.292210269322, 0.292210269322,
  0.372838941024, 0.372838941024,
  0.492530708782, 0.492530708782,
  0.635324635691, 0.635324635691,
  0.805584802014, 0.805584802014,
  1.0, 1.0
};

static const double YM_dac_table[] = {
  0.0, 0.0,
  0.00465400167849, 0.00772106507973,
  0.0109559777218, 0.0139620050355,
  0.0169985503929, 0.0200198367285,
  0.024368657969, 0.029694056611,
  0.0350652323186, 0.0403906309606,
  0.0485389486534, 0.0583352407111,
  0.0680552376593, 0.0777752346075,
  0.0925154497597, 0.111085679408,
  0.129747463188, 0.148485542077,
  0.17666895552, 0.211551079576,
  0.246387426566, 0.281101701381,
  0.333730067903, 0.400427252613,
  0.467383840696, 0.53443198291,
  0.635172045472, 0.75800717174,
  0.879926756695, 1.0
};

static void reset_segment(struct ayumi* ay);

static int update_tone(struct ayumi* ay, int index) {
  struct tone_channel* ch = &ay->channels[index];
  ch->tone_counter += 1;
  if (ch->tone_counter >= ch->tone_period) {
    ch->tone_counter = 0;
    ch->tone ^= 1;
  }
  return ch->tone;
}

static int update_noise(struct ayumi* ay) {
  int bit0x3;
  ay->noise_counter += 1;
  if (ay->noise_counter >= (ay->noise_period << 1)) {
    ay->noise_counter = 0;
    bit0x3 = ((ay->noise ^ (ay->noise >> 3)) & 1);
    ay->noise = (ay->noise >> 1) | (bit0x3 << 16);
  }
  return ay->noise & 1;
}

static void slide_up(struct ayumi* ay) {
  ay->envelope += 1;
  if (ay->envelope > 31) {
    ay->envelope_segment ^= 1;
    reset_segment(ay);
  }
}

static void slide_down(struct ayumi* ay) {
  ay->envelope -= 1;
  if (ay->envelope < 0) {
    ay->envelope_segment ^= 1;
    reset_segment(ay);
  }
}

static void hold_top(struct ayumi* ay) {
  (void) ay;
}

static void hold_bottom(struct ayumi* ay) {
  (void) ay;
}

static void (* const Envelopes[][2])(struct ayumi*) = {
  {slide_down, hold_bottom},
  {slide_down, hold_bottom},
  {slide_down, hold_bottom},
  {slide_down, hold_bottom},
  {slide_up, hold_bottom},
  {slide_up, hold_bottom},
  {slide_up, hold_bottom},
  {slide_up, hold_bottom},
  {slide_down, slide_down},
  {slide_down, hold_bottom},
  {slide_down, slide_up},
  {slide_down, hold_top},
  {slide_up, slide_up},
  {slide_up, hold_top},
  {slide_up, slide_down},
  {slide_up, hold_bottom}
};

static void reset_segment(struct ayumi* ay) {
  if (Envelopes[ay->envelope_shape][ay->envelope_segment] == slide_down
    || Envelopes[ay->envelope_shape][ay->envelope_segment] == hold_top) {
    ay->envelope = 31;
    return;
  }
  ay->envelope = 0;
}

int update_envelope(struct ayumi* ay) {
  ay->envelope_counter += 1;
  if (ay->envelope_counter >= ay->envelope_period) {
    ay->envelope_counter = 0;
    Envelopes[ay->envelope_shape][ay->envelope_segment](ay);
  }
  return ay->envelope;
}

static void update_mixer(struct ayumi* ay) {
  int i;
  int out;
  int noise = update_noise(ay);
  int envelope = update_envelope(ay);
  ay->sample = 0;
  for (i = 0; i < TONE_CHANNELS; i += 1) {
    out = (update_tone(ay, i) | ay->channels[i].t_off) & (noise | ay->channels[i].n_off);
    out *= ay->channels[i].e_on ? envelope : ay->channels[i].volume * 2 + 1;
    ay->sample += ay->dac_table[out];
  }
}

int ayumi_configure(struct ayumi* ay, int is_ym, double clock_rate, int sr) {
  int i;
  memset(ay, 0, sizeof(struct ayumi));
  ay->step = clock_rate / (sr * 8 * DECIMATE_FACTOR);
  ay->dac_table = is_ym ? YM_dac_table : AY_dac_table;
  ay->noise = 1;
  ayumi_set_envelope(ay, 1);
  for (i = 0; i < TONE_CHANNELS; i += 1) {
    ayumi_set_tone(ay, i, 1);
  }
  return ay->step < 1;
}

void ayumi_set_tone(struct ayumi* ay, int index, int period) {
  period &= 0xfff;
  ay->channels[index].tone_period = (period == 0) | period;
}

void ayumi_set_noise(struct ayumi* ay, int period) {
  period &= 0x1f;
  ay->noise_period = (period == 0) | period;
}

void ayumi_set_mixer(struct ayumi* ay, int index, int t_off, int n_off, int e_on) {
  ay->channels[index].t_off = t_off & 1;
  ay->channels[index].n_off = n_off & 1;
  ay->channels[index].e_on = e_on;
}

void ayumi_set_volume(struct ayumi* ay, int index, int volume) {
  ay->channels[index].volume = volume & 0xf;
}

void ayumi_set_envelope(struct ayumi* ay, int period) {
  period &= 0xffff;
  ay->envelope_period = (period == 0) | period;
}

void ayumi_set_envelope_shape(struct ayumi* ay, int shape) {
  ay->envelope_shape = shape & 0xf;
  ay->envelope_counter = 0;
  ay->envelope_segment = 0;
  reset_segment(ay);
}

static double decimate(double* x) {
  double y = -0.0000046183113992051936 * (x[1] + x[191]) +
    -0.00001117761640887225 * (x[2] + x[190]) +
    -0.000018610264502005432 * (x[3] + x[189]) +
    -0.000025134586135631012 * (x[4] + x[188]) +
    -0.000028494281690666197 * (x[5] + x[187]) +
    -0.000026396828793275159 * (x[6] + x[186]) +
    -0.000017094212558802156 * (x[7] + x[185]) +
    0.000023798193576966866 * (x[9] + x[183]) +
    0.000051281160242202183 * (x[10] + x[182]) +
    0.00007762197826243427 * (x[11] + x[181]) +
    0.000096759426664120416 * (x[12] + x[180]) +
    0.00010240229300393402 * (x[13] + x[179]) +
    0.000089344614218077106 * (x[14] + x[178]) +
    0.000054875700118949183 * (x[15] + x[177]) +
    -0.000069839082210680165 * (x[17] + x[175]) +
    -0.0001447966132360757 * (x[18] + x[174]) +
    -0.00021158452917708308 * (x[19] + x[173]) +
    -0.00025535069106550544 * (x[20] + x[172]) +
    -0.00026228714374322104 * (x[21] + x[171]) +
    -0.00022258805927027799 * (x[22] + x[170]) +
    -0.00013323230495695704 * (x[23] + x[169]) +
    0.00016182578767055206 * (x[25] + x[167]) +
    0.00032846175385096581 * (x[26] + x[166]) +
    0.00047045611576184863 * (x[27] + x[165]) +
    0.00055713851457530944 * (x[28] + x[164]) +
    0.00056212565121518726 * (x[29] + x[163]) +
    0.00046901918553962478 * (x[30] + x[162]) +
    0.00027624866838952986 * (x[31] + x[161]) +
    -0.00032564179486838622 * (x[33] + x[159]) +
    -0.00065182310286710388 * (x[34] + x[158]) +
    -0.00092127787309319298 * (x[35] + x[157]) +
    -0.0010772534348943575 * (x[36] + x[156]) +
    -0.0010737727700273478 * (x[37] + x[155]) +
    -0.00088556645390392634 * (x[38] + x[154]) +
    -0.00051581896090765534 * (x[39] + x[153]) +
    0.00059548767193795277 * (x[41] + x[151]) +
    0.0011803558710661009 * (x[42] + x[150]) +
    0.0016527320270369871 * (x[43] + x[149]) +
    0.0019152679330965555 * (x[44] + x[148]) +
    0.0018927324805381538 * (x[45] + x[147]) +
    0.0015481870327877937 * (x[46] + x[146]) +
    0.00089470695834941306 * (x[47] + x[145]) +
    -0.0010178225878206125 * (x[49] + x[143]) +
    -0.0020037400552054292 * (x[50] + x[142]) +
    -0.0027874356824117317 * (x[51] + x[141]) +
    -0.003210329988021943 * (x[52] + x[140]) +
    -0.0031540624117984395 * (x[53] + x[139]) +
    -0.0025657163651900345 * (x[54] + x[138]) +
    -0.0014750752642111449 * (x[55] + x[137]) +
    0.0016624165446378462 * (x[57] + x[135]) +
    0.0032591192839069179 * (x[58] + x[134]) +
    0.0045165685815867747 * (x[59] + x[133]) +
    0.0051838984346123896 * (x[60] + x[132]) +
    0.0050774264697459933 * (x[61] + x[131]) +
    0.0041192521414141585 * (x[62] + x[130]) +
    0.0023628575417966491 * (x[63] + x[129]) +
    -0.0026543507866759182 * (x[65] + x[127]) +
    -0.0051990251084333425 * (x[66] + x[126]) +
    -0.0072020238234656924 * (x[67] + x[125]) +
    -0.0082672928192007358 * (x[68] + x[124]) +
    -0.0081033739572956287 * (x[69] + x[123]) +
    -0.006583111539570221 * (x[70] + x[122]) +
    -0.0037839040415292386 * (x[71] + x[121]) +
    0.0042781252851152507 * (x[73] + x[119]) +
    0.0084176358598320178 * (x[74] + x[118]) +
    0.01172566057463055 * (x[75] + x[117]) +
    0.013550476647788672 * (x[76] + x[116]) +
    0.013388189369997496 * (x[77] + x[115]) +
    0.010979501242341259 * (x[78] + x[114]) +
    0.006381274941685413 * (x[79] + x[113]) +
    -0.007421229604153888 * (x[81] + x[111]) +
    -0.01486456304340213 * (x[82] + x[110]) +
    -0.021143584622178104 * (x[83] + x[109]) +
    -0.02504275058758609 * (x[84] + x[108]) +
    -0.025473530942547201 * (x[85] + x[107]) +
    -0.021627310017882196 * (x[86] + x[106]) +
    -0.013104323383225543 * (x[87] + x[105]) +
    0.017065133989980476 * (x[89] + x[103]) +
    0.036978919264451952 * (x[90] + x[102]) +
    0.05823318062093958 * (x[91] + x[101]) +
    0.079072012081405949 * (x[92] + x[100]) +
    0.097675998716952317 * (x[93] + x[99]) +
    0.11236045936950932 * (x[94] + x[98]) +
    0.12176343577287731 * (x[95] + x[97]) +
    0.125 * x[96];
  memcpy(&x[FIR_SIZE - DECIMATE_FACTOR], x, DECIMATE_FACTOR * sizeof(double));
  return y;
}

void ayumi_process(struct ayumi* ay) {
  int i;
  double y1;
  double* c = ay->interpolator.c;
  double* y = ay->interpolator.y;
  double* fir = &ay->fir[FIR_SIZE - ay->fir_index * DECIMATE_FACTOR];
  ay->fir_index = (ay->fir_index + 1) % (FIR_SIZE / DECIMATE_FACTOR - 1);
  for (i = DECIMATE_FACTOR - 1; i >= 0; i -= 1) {
    ay->x += ay->step;
    if (ay->x >= 1) {
      ay->x -= 1;
      y[0] = y[1];
      y[1] = y[2];
      y[2] = y[3];
      update_mixer(ay);
      y[3] = ay->sample;
      y1 = y[2] - y[0];
      c[0] = 0.5 * y[1] + 0.25 * (y[0] + y[2]);
      c[1] = 0.5 * y1;
      c[2] = 0.25 * (y[3] - y[1] - y1);
    }
    fir[i] = (c[2] * ay->x + c[1]) * ay->x + c[0];
  }
  ay->sample = decimate(fir);
}

static double dc_filter(struct dc_filter* dc, int index, double x) {
  dc->sum += -dc->delay[index] + x;
  dc->delay[index] = x; 
  return x - dc->sum / DC_FILTER_SIZE;
}

void ayumi_remove_dc(struct ayumi* ay) {
  ay->sample = dc_filter(&ay->dc, ay->dc_index, ay->sample);
  ay->dc_index = (ay->dc_index + 1) & (DC_FILTER_SIZE - 1);
}

/*
    process port I/O data
*/
void psg_out(struct ayumi* ay, int register_select, BYTE data)
{
    printf("\nreg %d data %02X\n\r", register_select, data);
        
    switch(register_select) {
        case 0: /* channel A fine tune */
                ay->channels[0].tone_period &= ~0xff;
                ay->channels[0].tone_period |= data;
                break;
        case 1: /* channel A coarse tune */
                ay->channels[0].tone_period &= 0xff;
                ay->channels[0].tone_period |= (data & 0xf) << 8;
                break;
        case 2: /* channel B fine tune */
                ay->channels[1].tone_period &= ~0xff;
                ay->channels[1].tone_period |= data;
                break;
        case 3: /* channel B coarse tune */
                ay->channels[1].tone_period &= 0xff;
                ay->channels[1].tone_period |= (data & 0xf) << 8;
                break;
        case 4: /* channel C fine tune */
                ay->channels[2].tone_period &= ~0xff;
                ay->channels[2].tone_period |= data;
                break;
        case 5: /* channel C coarse tune */
                ay->channels[2].tone_period &= 0xff;
                ay->channels[2].tone_period |= (data & 0xf) << 8;
                break;
        case 6: /* noise period */
                ay->noise_period = data & 0x1f;
                break;
        case 7: /* mixer control */
                ay->channels[0].t_off = data & 1;
                ay->channels[1].t_off = (data >> 1) & 1;
                ay->channels[2].t_off = (data >> 2) & 1;
                ay->channels[0].n_off = (data >> 3) & 1;
                ay->channels[1].n_off = (data >> 4) & 1;
                ay->channels[2].n_off = (data >> 5) & 1;
                break;
        case 8: /* amplitude A */
                ay->channels[0].e_on = (data >> 4) & 1;
                ay->channels[0].volume = data & 0xf;
                break;
        case 9: /* amplitude B */
                ay->channels[1].e_on = (data >> 4) & 1;
                ay->channels[1].volume = data & 0xf; 
                break;
        case 10: /* amplitude C */
                ay->channels[2].e_on = (data >> 4) & 1;
                ay->channels[2].volume = data & 0xf; 
                break;
        case 11: /* envelope fine tune */
                ay->envelope_period &= ~0xff;
                ay->envelope_period |= data;
                break;
        case 12: /* envelope coarse tune */
                ay->envelope_period &= 0xff;
                ay->envelope_period |= data << 8;
                break;
        case 13: /* envelope shape */
                ay->envelope_shape = data & 0xf;
                ay->envelope_counter = 0;
                ay->envelope_segment = 0;
                reset_segment(ay);
                break;
        case 14: /* I/O port A (unused) */
                break;
        case 15: /* I/O port B (unused) */
                break;
    }
}

/* ---------------End AY-3-8910 -------------- */

typedef struct {
    int16_t channel_1;
    int16_t channel_2;
} SampleData;

struct ads_noisemaker {
    struct ayumi psg1;			/* PSG 1 (left channel) */
    struct ayumi psg2;			/* PSG 2 (right channel) */
    SampleData *buffer;			/* recorded data for WAV output */
    int index;                          /* index into recorded data */
    long size;				/* buffer size in samples  */
};

static struct ads_noisemaker sound_board;
static int psg_register_select_1 = 0;
static int psg_register_select_2 = 0;

#ifdef WANT_SDL

static SDL_AudioDeviceID device_id;

void sdl_audio_callback(void *userdata, uint8_t *stream, int len)
{
    /* cast data passed through stream to our structure. */
    struct ads_noisemaker *board = (struct ads_noisemaker *)userdata;
    struct ayumi *ay1 = &(board->psg1);
    struct ayumi *ay2 = &(board->psg2);
    int16_t *out = (int16_t *) stream;
    int i;

    if (len == 0) return;

    i = 0;
    while(i<len) {
        /* process PSG data */
        ayumi_process(ay1);
        ayumi_process(ay2);
        ayumi_remove_dc(ay1);
        ayumi_remove_dc(ay2);

	/* stream audio data */
        *out++ = (int16_t)(ay1->sample * 32767);           /* channel 1 */
        *out++ = (int16_t)(ay2->sample * 32767);           /* channel 2 */
        i += 4;
  
  	/* save into wave buffer */
        if (board->index < board->size) {
            board->buffer[board->index].channel_1 = (int16_t)(ay1->sample * 32767);
            board->buffer[board->index].channel_2 = (int16_t)(ay2->sample * 32767);
            board->index++;
        }
    }
}

static SDL_AudioDeviceID sdl_audio_init(void)
{
	SDL_AudioSpec desired, obtained;
	SDL_AudioDeviceID device_id;

	/* prepare audio properties for streaming */
	desired.freq = noisemaker_sample_rate;
	desired.format = AUDIO_S16SYS;
	desired.channels = 2;
	desired.samples = SAMPLE_BUFFER_SIZE;
	desired.padding = 0;
	desired.callback = sdl_audio_callback;
	desired.userdata = &sound_board;

	/* start streaming */
	device_id = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(0, 0), 0, &desired, &obtained, 0);
	if (device_id == 0) {
		fprintf(stderr, "SDL: Failed to open audio device: %s\n", SDL_GetError());
	}
	else {
		/* start audio device */
		SDL_PauseAudioDevice(device_id, 0);
	}
	
	return device_id;
}

static void sdl_audio_off(SDL_AudioDeviceID device_id)
{
	/* shutdown SDL audio  */
	SDL_CloseAudioDevice(device_id);
}

#endif	/* WANT_SDL */

#ifdef WANT_PORTAUDIO

static PaStream *stream;

/* This routine will be called by the PortAudio engine when audio is needed.
** It may called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int paCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    /* cast data passed through stream to our structure. */
    struct ads_noisemaker *board = (struct ads_noisemaker *)userData;
    struct ayumi *ay1 = &(board->psg1);
    struct ayumi *ay2 = &(board->psg2);
    float *out = (float*)outputBuffer;
    unsigned int i;

    /* prevent unused variable warning. */
    (void) inputBuffer;
    (void) timeInfo;
    (void) statusFlags;
    
    /* process buffer */
    for( i=0; i<framesPerBuffer; i++ )
    {
        ayumi_process(ay1);
        ayumi_process(ay2);
        ayumi_remove_dc(ay1);
        ayumi_remove_dc(ay2);

        *out++ = (float) ay1->sample;           /* channel 1 */
        *out++ = (float) ay2->sample;           /* channel 2 */
  
        if (board->index < board->size) {
            board->buffer[board->index].channel_1 = (int16_t)(ay1->sample * 32767);
            board->buffer[board->index].channel_2 = (int16_t)(ay2->sample * 32767);
            board->index++;
        }
    }

    return 0;
}

static int portaudio_init(void) {
    PaError err;

    /* Initialize library before making any other calls. */
    err = Pa_Initialize();
    if( err != paNoError ) {
    	fprintf(stderr, "\nPortAudio: Could not initialize PortAudio\n");
    	goto error;
    }

    /* Open an audio I/O stream. */
    err = Pa_OpenDefaultStream( &stream,
                                0,            /* no input channels */
                                2,            /* stereo output */
                                paFloat32,    /* floating point output */
                                noisemaker_sample_rate,
                                256,          /* frames per buffer */
                                paCallback,
                                &sound_board );
    if( err != paNoError ) {
    	fprintf(stderr, "\nPortAudio: Could not open default stream\n");
    	goto error;
    }

    err = Pa_StartStream( stream );
    if( err != paNoError ) {
    	fprintf(stderr, "\nPortAudio: Could not start stream\n");
    	goto error;
    }
        
    sound_board.index = 0;
        
    return 0;

error:
    Pa_Terminate();
    return -1;
}

static void portaudio_stop(void) {
    PaError err;

    err = Pa_StopStream( stream );
    if( err != paNoError ) goto error;
    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto error;

    return;

error:
    Pa_Terminate();
    return;
}

static void portaudio_shutdown(void)
{
    /* shutdown PortAudio */
    portaudio_stop(); 
}

#endif		/* WANT_PORTAUDIO */

/* -------------- End PortAudio ---------------- */

void ads_noisemaker_init(void) {

    inPort[0] = 0xFF;

    struct ayumi* ay1 = &(sound_board.psg1);
    struct ayumi* ay2 = &(sound_board.psg2);

#ifdef HAS_NETSERVER
    if (n_flag) {
        net_device_service(DEV_NMKR, ads_noisemaker_callback);
    }
#endif

#ifdef WANT_SDL
    if (!(device_id = sdl_audio_init())) {
    	    LOG(TAG, "ADS Noisemaker: Could initialize\r\n");
    	    return;
    }
#endif

#ifdef WANT_PORTAUDIO
    /* initialize PortAudio */
    if (portaudio_init() < 0) {
    	    LOG(TAG, "ADS Noisemaker: Could not initialize\r\n");
    	    return;
    };
#endif
    
    sound_board.size = 0;
    if (noisemaker_recording_limit > 0) {
	if ((sound_board.buffer = malloc(noisemaker_recording_limit * sizeof(SampleData))) == NULL) {
		LOG(TAG, "ADS Noisemaker: Could not allocate enough memory for recording, reduce recording limit\n");
	};
	sound_board.size = noisemaker_recording_limit;
    };
    sound_board.index = 0;

    /* prepare both AY-3-8910 PSGs */
    ayumi_configure(ay1, 0, 2000000, noisemaker_sample_rate);
    ayumi_configure(ay2, 0, 2000000, noisemaker_sample_rate);
    ayumi_set_mixer(ay1, 0, 0, 1, 0);
    ayumi_set_volume(ay1, 0, 0xf);
    ayumi_set_mixer(ay2, 0, 0, 1, 0);
    ayumi_set_volume(ay2, 0, 0xf);

    LOG(TAG, "ADS Noisemaker initialized\r\n");
}

void ads_noisemaker_off(void)
{
    /* ---------- export as wave file ---------- */

    int16_t buf[2048];
    int i, buf_index, data_size;

#pragma pack(1)
    
	struct {
		char chunk_id[4];
		uint32_t chunk_size;
		char format[4];
		char subchunk1_id[4];
		uint32_t subchunk1_size;
		uint16_t audio_format;
		uint16_t num_channels;
		uint32_t sample_rate;
		uint32_t byte_rate;
		uint16_t block_align;
		uint16_t bits_per_sample;
		char subchunk2_id[4];
		uint32_t subchunk2_size;
	} header;

#pragma pack()
    
    if (noisemaker_soundfile && sound_board.buffer) {

	/* open wave file */
	FILE *fp = fopen(noisemaker_soundfile, "wb");
	if (!fp) {
	    printf("Couldn't open file %s\n", noisemaker_soundfile);
	    return;
	}
	else {
		data_size = sound_board.index * 4;
		
		/* setup wave header */
		memcpy(header.chunk_id, "RIFF", 4);
		header.chunk_size = data_size + 40;
		memcpy(header.format, "WAVE", 4);
		memcpy(header.subchunk1_id, "fmt ", 4);
		header.subchunk1_size = 16;
		header.audio_format = 1;
		header.num_channels = 2;
		header.sample_rate = noisemaker_sample_rate;
		header.byte_rate = noisemaker_sample_rate * 4;
		header.block_align = 4;
		header.bits_per_sample = 16;
		memcpy(header.subchunk2_id, "data", 4);
		header.subchunk2_size = data_size;
		
		/* write wave header */
		fwrite(&header, sizeof(header), 1, fp);
		
		/* write sample data */
		buf_index = 0;
		for (i=0; i<sound_board.index; i++) {
			buf[buf_index++] = sound_board.buffer[i].channel_1;
			buf[buf_index++] = sound_board.buffer[i].channel_2;
			if (buf_index == 1024) {
		        	buf_index = 0;
		        	fwrite(buf, 1024, 2, fp);
			}
		}
		if (buf_index > 0) fwrite(buf, buf_index, 2, fp);
	
	    	fclose(fp);
	}
    }

    free(sound_board.buffer);

#ifdef WANT_PORTAUDIO
    portaudio_shutdown();
#endif

#ifdef WANT_SDL
    sdl_audio_off(device_id);
#endif

    printf("ADS Noisemaker shut down\r\n");
}

static void ads_noisemaker_out(BYTE port, BYTE data)
{
    outPort[port] = data;
    struct ayumi *ay1 = &(sound_board.psg1);
    struct ayumi *ay2 = &(sound_board.psg2);

    LOGD(TAG, "Output %02X on port %02X", data, port);

#ifdef HAS_NETSERVER
    if (n_flag) {
        // if (net_device_alive(DEV_NMKR)) {
            net_device_send(DEV_NMKR, (char *)&data, 1);
        // }
    }
#endif

    /* ---------- Noisemaker ---------- */

    /* update PSG status */
    switch(port) {
    case 0:     /* select PSG register of first PSG */
            psg_register_select_1 = data & 0xf;
            break;
    case 1:     /* output data first PSG */
            psg_out(ay1, psg_register_select_1, data);
            break;
    case 2:     /* select PSG register of second PSG */
            psg_register_select_2 = data & 0xf;
            break;
    case 3:     /* output data second PSG */
            psg_out(ay2, psg_register_select_2, data);
            break;
    default:    /* ignore all other ports */
            ;
    }
}

void ads_noisemaker_0_out(BYTE data) { ads_noisemaker_out(0, data); }
void ads_noisemaker_1_out(BYTE data) { ads_noisemaker_out(1, data); }
void ads_noisemaker_2_out(BYTE data) { ads_noisemaker_out(2, data); }
void ads_noisemaker_3_out(BYTE data) { ads_noisemaker_out(3, data); }

#endif /* HAS_NOISEMAKER*/