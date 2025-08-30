/*
 * cromemco-d+7a.c
 *
 * Emulation of the Cromemco D+7A I/O
 *
 * Copyright (C) 2020 by David McNaughton
 * Copyright (C) 2025 by Ansgar Kueckes
 *
 * History:
 * 14-JAN-2020	1.0	Initial Release
 * 06-JUN-2025		Audio and joystick support based on SDL2 and (optionally) PortAudio
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sys/time.h"

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"

#ifdef WANT_SDL
#include <SDL.h>
#include "simsdl.h"
#endif

#ifdef WANT_PORTAUDIO
#include "portaudio.h"
#endif

#ifdef HAS_NETSERVER
#include "netsrv.h"
#endif
#include "cromemco-d+7a.h"

// #define LOG_LOCAL_LEVEL LOG_DEBUG
#include "log.h"
static const char *TAG = "D+7AIO";

#define PORT_COUNT  8

static BYTE inPort[PORT_COUNT];
static BYTE outPort[PORT_COUNT];

/*
    Two buffers are serviced by writing with fixed rate to port 0x19
    (channel 1) and port 0x1b (channel 2). The ring buffers work as source
    for the audio stream, which is fed by a callback function being
    called each time the audio stream buffer needs new data.
    
    Currently, with PortAudio and SDL2 Audio two common audio subsystems are
    supported, which again work as a frontend for a number of other low level
    sound frameworks, such as PulseAudio or ALSA.
    
    The emulation can be configured to create a recording of the sound output
    during playback, which is provided in a WAV file after the emulation has
    stopped.
*/

#define NUM_CHANNELS 		2		/* number of channels, 2 for stereo */
#define DEFAULT_SAMPLE_RATE	22050		/* default SDL audio sample rate in Hz */
#define DEFAULT_BUFFER_SIZE	64		/* default audio buffer size in samples per channel, defines audio delay */
#define DEFAULT_SYNC_ADJUST	1.0247		/* default fine tuning for audio sync adjustment */
#define DEFAULT_RECORDING_LIMIT 10000000	/* size of wave buffer for file output & debug purposes */
#define RING_BUFFER_SIZE	4048		/* ring buffer size per channel */

/* parameters configurable in system.conf */
double d7a_sync_adjust = DEFAULT_SYNC_ADJUST;
long d7a_sample_rate = DEFAULT_SAMPLE_RATE;
long d7a_recording_limit = DEFAULT_RECORDING_LIMIT;
long d7a_buffer_size = DEFAULT_BUFFER_SIZE;
char *d7a_soundfile = NULL;
bool d7a_stats = false;

typedef struct {
	char sample[NUM_CHANNELS];		/* actual sample for each channel */
	Tstates_t tick[NUM_CHANNELS];		/* CPU tick for each channel */
	unsigned int count[NUM_CHANNELS];	/* buffer occupation for each channel */
	uint8_t status;				/* 0=OK, 1=underflow, 2=overflow, 3=dropout, 4=timeout */
} DebugData;

static DebugData *wave_buffer = NULL;
static int wave_index[NUM_CHANNELS];

typedef struct {
        char sample[RING_BUFFER_SIZE];  	/* actual ring buffer for samples */
        int start;                		/* index to first entry in ring buffer */
        int end;                        	/* index to next free buffer entry */
        int count;				/* number of samples in buffer */
} SampleBuffer;

typedef struct {
        int lock;				/* locks buffer changes during callback */
        int last_count;				/* buffer occupation at last callback */
	SampleBuffer channel[NUM_CHANNELS];	/* left audio channel */
} RingBuffer;

static RingBuffer ring_buffer;			/* intermediate buffer for port data */
static char last_data[NUM_CHANNELS];		/* current sample for each channel */
static Tstates_t last_time[NUM_CHANNELS];	/* time stamp of last port command */
static double timing_error[NUM_CHANNELS];	/* cumulated timing error in each channel */

static int underflows = 0;			/* statistics */
static int overflows = 0;
static int dropouts = 0;
static int timeouts = 0;

#ifdef WANT_SDL

static SDL_AudioDeviceID device_id;

/*
   This routine will be called by the SDL2 audio framework each time new data
   for playback on an audio device is requested.
*/
static void sdl_audio_callback(void *userdata, uint8_t *stream, int len)
{
    RingBuffer *ring_buffer = (RingBuffer*)userdata;
    int i, c, max, gap, count;

    /* return if ring buffer is in use or no data is available */
    if (ring_buffer->lock || (len == 0)) return;
    	
    max = 0;
    for (c=0; c<NUM_CHANNELS; c++) {
    	count = ring_buffer->channel[c].count;
    	if (count > max) max = count;
    }
    
    i = 0;

    /* special handling for partially filled ring buffer */
    if (ring_buffer->last_count == 0) {

	/* sort data to end of buffer in order to avoid audio breaks */
    	gap = len - (max * NUM_CHANNELS);
    	while (i<gap) {
	    for (c=0; c<NUM_CHANNELS; c++) {
	    	stream[i++] = 0;	/* silence */
	    }
    	}
        ring_buffer->last_count = max;
    }
    
    /* copy ring buffer to SDL audio stream */
    while (i<len) {
	for (c=0; c<NUM_CHANNELS; c++) {
	        if (ring_buffer->channel[c].count == 0) {
		    stream[i++] = 0;	/* silence */
	        }
	        else {
	            /* copy ring buffer to audio buffer */
		    stream[i++] = ring_buffer->channel[c].sample[ring_buffer->channel[c].start];
	            ring_buffer->channel[c].count--;
	            ring_buffer->channel[c].start++;
	            if (ring_buffer->channel[c].start >= RING_BUFFER_SIZE)
	            	ring_buffer->channel[c].start = 0;
		}
	}
    }
}

static SDL_AudioDeviceID sdl_audio_init(void)
{
	SDL_AudioSpec desired;

	/* prepare audio properties for streaming */
	desired.freq = d7a_sample_rate;
	desired.format = AUDIO_S8;
	desired.channels = NUM_CHANNELS;
	desired.samples = d7a_buffer_size;
	desired.padding = 0;
	desired.callback = sdl_audio_callback;
	desired.userdata = &ring_buffer;

	/* start streaming (actually, Mix_SetPostMix() should be used for not confusing applications using the SDL2 mixer) */
	device_id = SDL_OpenAudioDevice(0, 0, &desired, 0, 0);
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

/*
   This routine will be called by the PortAudio engine when audio is needed.
   It may called at interrupt level on some machines so don't do anything
   that could mess up the system like calling malloc() or free().
 */
static int paCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    RingBuffer *ring_buffer = (RingBuffer*)userData;
    char *out = (char*)outputBuffer;
    unsigned int i, gap, c;
    unsigned long max;

    /* prevent unused variable warning. */
    (void) inputBuffer;
    (void) timeInfo;
    (void) statusFlags;
    
    /* return if ring buffer is in use or no data is available */
    if (ring_buffer->lock || (framesPerBuffer == 0) || (out == NULL)) return 0;
    	
    /* lock ring buffer */
    ring_buffer->lock = 1;

    i = 0;

    /* special handling for partially filled ring buffer */
    if (ring_buffer->last_count == 0) {

	/* sort data to end of buffer in order to avoid audio breaks */
    	max = 0;
    	for (c=0; c<NUM_CHANNELS; c++)
    		if (ring_buffer->channel[c].count > (int)max) max = ring_buffer->channel[c].count;
    	ring_buffer->last_count = max;
    	if (max < framesPerBuffer) {
    	    gap = framesPerBuffer - max;
    	    for (;i<gap; i++) {
	    	for (c=0; c<NUM_CHANNELS; c++) {
	            *out++ = 0;	/* silence */
	        }
    	    }
	}
    }

    /* copy ring buffer to SDL audio stream */
    while (i<framesPerBuffer) {
	for (c=0; c<NUM_CHANNELS; c++) {
	        if (ring_buffer->channel[c].count == 0) {
	            *out++ = 0;		/* silence */
	        }
	        else {
	            /* copy ring buffer to audio buffer */
	            *out++ = (float)ring_buffer->channel[c].sample[ring_buffer->channel[c].start];
	            ring_buffer->channel[c].count--;
	            ring_buffer->channel[c].start++;
	            if (ring_buffer->channel[c].start >= RING_BUFFER_SIZE)
	            	ring_buffer->channel[c].start = 0;
		}
	}
	i++;
    }

    /* unlock ring buffer */
    ring_buffer->lock = 0;

    return 0;
}

static int portaudio_init(void) {
    PaError err;
    PaStreamParameters outputParameters;

    /* Initialize library before making any other calls. */
    err = Pa_Initialize();
    if( err != paNoError ) {
    	fprintf(stderr, "\nPortAudio: Could not initialize PortAudio\n");
    	goto error;
    }

    outputParameters.device                    = Pa_GetDefaultOutputDevice();
    outputParameters.channelCount              = NUM_CHANNELS;
    outputParameters.sampleFormat              = paInt8;                /* 8 bit integer output. */
    outputParameters.suggestedLatency          = 0.2;			/* 200 ms */
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
                  &stream,
                  NULL, /* no input */
                  &outputParameters,
                  d7a_sample_rate,
                  d7a_buffer_size,
                  paClipOff,
                  paCallback,
                  &ring_buffer );

#if 0
    /* Open an audio I/O stream. */
    err = Pa_OpenDefaultStream( &stream,
                                0,           	 	/* no input channels */
                                NUM_CHANNELS,   	/* stereo output */
                                paInt8,    		/* floating point output */
                                d7a_sample_rate,	/* sample rate */
                                256,          		/* frames per buffer */
                                paCallback,		/* callback */
                                &ring_buffer );
#endif

    if( err != paNoError ) {
    	fprintf(stderr, "\nPortAudio: Could not open default stream\n");
    	goto error;
    }

    err = Pa_StartStream( stream );
    if( err != paNoError ) {
    	fprintf(stderr, "\nPortAudio: Could not start stream\n");
    	goto error;
    }
        
    return 0;

error:
    Pa_Terminate();
    return -1;
}

static void portaudio_off(void) {
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

#endif	/* WANT_PORTAUDIO */

/*
    Record a wave level from a specified audio port channel as realtime
    data into a ring buffer.

    For appropriate sound generation, it is required to be 100% in sync
    with the emulator's CPU state clock.
    
    In order to do so, we calculate the time difference between the last
    write to the port and the current write from the number of CPU state
    cycles between both writes, divided by the nominal CPU frequency.
    
    We then map the port write timing to the sampling rate we are using
    for streaming. If there are multiple sampling events between two port
    writes, the missing samples will be interpolated.
    
    A challenge is the method z80pack is using for re-syncing the CPU
    (roughly) every 10 ms, which creates long breaks and requires adaquate
    buffering.
    
    Both buffer underflows and overflows of course impact sound quality.
    It is desirable to have the perfect balance minimizing both by selecting
    the proper value for d7a_sync_adjust, which can be configured in the
    system.conf file. Run the audio application of your choice, while
    changing that value until you achieve the best value for both underflows
    and overflows.
    
    Dropouts happen if the continuous data stream from the application
    program to the D7+A hardware is affected, so that the output does
    not change over a noticeable number of samples. This can be caused
    by the application program and/or can be related to the emulation of
    the 8080/Z80 CPU.
*/
void cromemco_d7a_record(int port, char data)
{
    int i, c, count = 0;
    uint64_t timeout;
    Tstates_t current_time;
    double slope, current_level, ratio, diff;
    
    /* save current CPU state clock */
    current_time = T;

    /* create a timing reference sample rate vs emulated CPU clock */
    ratio = d7a_sample_rate / (f_value * 1000000.0) * d7a_sync_adjust;

    /* assign port 1 to channel 0, and port 3 to channel 1 */
    c = 0;
    if (NUM_CHANNELS > 1) {
	    switch(port) {
	    	case 1: c = 0; break;
	    	case 3: c = 1; break;
	    	default: c = 0;
	    }
    }
        
    /* reset sample status to normal */
    if (wave_buffer) wave_buffer[wave_index[c]].status = 0;

    /* determine the number of samples since the last port write */
    if (last_time[c] == 0) {
      	last_time[c] = current_time;
    }
    diff = (current_time - last_time[c]) * ratio;
    count = diff;
    timing_error[c] += diff - count;
    if (timing_error[c] >= 1.0) {
      	count++;
       	timing_error[c] -= 1.0;
    }
    last_time[c] = current_time;
    
#ifdef WANT_SDL
    SDL_LockAudioDevice(device_id);
    UNUSED(timeout);
#else
    /* wait for being unlocked */
    timeout = 1000000;
    while(ring_buffer.lock && !timeout) timeout--;
    if (!timeout) {
	if (wave_buffer) wave_buffer[wave_index[c]-1].status = 4;
	timeouts++;
    	return;
    }
    ring_buffer.lock = 1;
#endif

    if (ring_buffer.channel[c].count == 0) underflows++;

    if (count > (RING_BUFFER_SIZE - ring_buffer.channel[c].count)) {
        /* drop new data */
        count = RING_BUFFER_SIZE - ring_buffer.channel[c].count;
        if (wave_buffer) wave_buffer[wave_index[c]-1].status = 1;	/* overflow */
        overflows++;
    }
    else if (count > 5) {
        if (wave_buffer) wave_buffer[wave_index[c]-1].status = 2; 	/* noticeable dropout */
        dropouts++;
    }

    /* append to ring buffer (time interpolated) */
    if (count == 1) {
        /* append sample to ring buffer */
        ring_buffer.channel[c].sample[ring_buffer.channel[c].end++] = (char)data;
        if (ring_buffer.channel[c].end >= RING_BUFFER_SIZE)
	       	ring_buffer.channel[c].end = 0;

        /* wave output / debug log */
        if (wave_buffer) {
	        wave_buffer[wave_index[c]].sample[c] = (char)data;
	        wave_buffer[wave_index[c]].count[c] = ring_buffer.channel[c].count + 1;
	        wave_buffer[wave_index[c]].tick[c] = current_time;
	        if (wave_index[c] < d7a_recording_limit - 1) wave_index[c]++;
        }
    }
    else if (count < 5) {
        /* apppend group of samples (interpolated) to ring buffer */
        current_level = (double)last_data[c];
        slope = ((double)data - (double)last_data[c]) / (double)count;
        for (i=0; i<count; i++) {
		ring_buffer.channel[c].sample[ring_buffer.channel[c].end++] = (char)current_level;
		if (ring_buffer.channel[c].end >= RING_BUFFER_SIZE)
	            	ring_buffer.channel[c].end = 0;
	        current_level += slope;
	
		/* wave output / debug log */
		if (wave_buffer) {
			wave_buffer[wave_index[c]].sample[c] = (char)current_level;
			wave_buffer[wave_index[c]].count[c] = ring_buffer.channel[c].count + i;
			wave_buffer[wave_index[c]].tick[c] = current_time;
			if (wave_index[c] < d7a_recording_limit - 1) wave_index[c]++;
		}
        }
    }
    else {
         /* append silence to ring buffer */
         for (i=0; i<count; i++) {
         	ring_buffer.channel[c].sample[ring_buffer.channel[c].end++] = 0;
         	if (ring_buffer.channel[c].end >= RING_BUFFER_SIZE)
            		ring_buffer.channel[c].end = 0;

         	/* wave output / debug log */
		if (wave_buffer) {
			wave_buffer[wave_index[c]].sample[c] = 0;
	        	wave_buffer[wave_index[c]].count[c] = ring_buffer.channel[c].count + i;
			wave_buffer[wave_index[c]].tick[c] = current_time;
			if (wave_index[c] < d7a_recording_limit - 1) wave_index[c]++;
		}
    	}
    }
    ring_buffer.channel[c].count += count;
    
#ifdef WANT_SDL
    SDL_UnlockAudioDevice(device_id);
#else
    ring_buffer.lock = 0;
#endif
    

    if (port == 1) {
	last_data[0] = data;
    }
    else if (NUM_CHANNELS > 1) {
	last_data[1] = data;
    }
}

#ifdef HAS_NETSERVER
static void cromemco_d7a_callback(BYTE *data)
{
	int i;

	inPort[0] = *data++;
	for (i = 1; i < PORT_COUNT; i++)
		inPort[i] = (*data++) - 128;
}
#endif

void cromemco_d7a_init(void)
{
	inPort[0] = 0xFF;
	int c;

#ifdef HAS_NETSERVER
	if (n_flag)
		net_device_service(DEV_D7AIO, cromemco_d7a_callback);
#endif

	if (d7a_recording_limit > 0) {
		wave_buffer = malloc(d7a_recording_limit * sizeof(DebugData));
		if (wave_buffer == NULL) {
			fprintf(stderr, "Could not allocate enough memory for recording, please reduce recording limit\n");
		};
	};

	ring_buffer.last_count = 0;
	for (c=0; c<NUM_CHANNELS; c++) {
		last_time[c] = 0;
		timing_error[c] = 0.0;
		ring_buffer.channel[c].start = 0;
		ring_buffer.channel[c].end = 0;
		ring_buffer.channel[c].count = 0;
		memset(ring_buffer.channel[c].sample, 0, RING_BUFFER_SIZE);
	}

#ifdef WANT_SDL
    if (sdl_num_joysticks > 0) {
    	if (sdl_num_joysticks == 1) {
    	    LOG(TAG, "D+7A: 1 joystick connected\n");
    	}
    	else {
    	    LOG(TAG, "D+7A: %d joysticks connected\n", sdl_num_joysticks);
    	}
    }
    else {
    	    LOG(TAG, "D+7A: No joystick connected\n");
    }
    if ((device_id = sdl_audio_init())) {
    	    LOG(TAG, "D+7A: SDL audio initialized & ready to use\n");
    }
    else {
    	    LOG(TAG, "D+7A: Could not initialize SDL audio\n");
    	    return;
    }
#endif

#ifdef WANT_PORTAUDIO
    /* initialize PortAudio */
    if (portaudio_init() >= 0) {
    	    LOG(TAG, "D+7A: PortAudio initialized & ready to use\n");
    }
    else {
    	    LOG(TAG, "D+7A: Could not initialize PortAudio\n");
    	    return;
    };
#endif
}

void cromemco_d7a_off(void)
{
	int i, c;
	int16_t buf[1024];
	int buf_index = 0;
	int max_index = 0;
	FILE *fp;
	
	/* ---------- export as wave file ---------- */
	
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

	for (c=0; c<NUM_CHANNELS; c++) {
		if (wave_index[c] > max_index) max_index = wave_index[c];
	}
	
	if (d7a_soundfile && wave_buffer) {

		/* open wave file */
		fp = fopen(d7a_soundfile, "wb");
		if (fp) {
			/* setup wave header */
			memcpy(header.chunk_id, "RIFF", 4);
			header.chunk_size = max_index * 4 + 40;
			memcpy(header.format, "WAVE", 4);
			memcpy(header.subchunk1_id, "fmt ", 4);
			header.subchunk1_size = 16;
			header.audio_format = 1;
			header.num_channels = NUM_CHANNELS;
			header.sample_rate = d7a_sample_rate;
			header.byte_rate = d7a_sample_rate * 4;
			header.block_align = 4;
			header.bits_per_sample = 16;
			memcpy(header.subchunk2_id, "data", 4);
			header.subchunk2_size = max_index * 4;
			
			/* write wave header */
			fwrite(&header, sizeof(header), 1, fp);
		
			/* write wave data */
			for (i=0; i<max_index; i++) {
				for (c=0; c<NUM_CHANNELS; c++) {
					buf[buf_index++] = wave_buffer[i].sample[c] * 256;
				}
				if (buf_index == 1024) {
			        	buf_index = 0;
			        	fwrite(buf, 1024, 2, fp);
				}
			}
			if (buf_index > 0) fwrite(buf, buf_index, 2, fp);
	
			fclose(fp);
		}
		else {
			printf("Couldn't open file %s\n", d7a_soundfile);
		}
		free(d7a_soundfile);
	}
	
	if (d7a_stats) {
    	    LOG(TAG, "D7A stats: underflows: %d overflows: %d dropouts: %d timeouts: %d\n",
		underflows, overflows, dropouts, timeouts);
	}
	
	if (wave_buffer) free(wave_buffer);

#ifdef WANT_SDL
	sdl_audio_off(device_id);
#endif

#ifdef WANT_PORTAUDIO
	portaudio_off();
#endif
}

static void cromemco_d7a_out(BYTE port, BYTE data)
{
	outPort[port] = data;

	LOGD(TAG, "Output %d on port %d", data, port);

#ifdef HAS_NETSERVER
	if (n_flag) {
		// if (net_device_alive(DEV_D7AIO)) {
		net_device_send(DEV_D7AIO, (char *) &data, 1);
		// }
	}
#endif

	/* feed audio data into ring buffer */
	if ((port == 1) || (port == 3)) {	
	        cromemco_d7a_record(port, data);
	}
}

void cromemco_d7a_D_out (BYTE data) { cromemco_d7a_out(0, data); }
void cromemco_d7a_A1_out(BYTE data) { cromemco_d7a_out(1, data); }
void cromemco_d7a_A2_out(BYTE data) { cromemco_d7a_out(2, data); }
void cromemco_d7a_A3_out(BYTE data) { cromemco_d7a_out(3, data); }
void cromemco_d7a_A4_out(BYTE data) { cromemco_d7a_out(4, data); }
void cromemco_d7a_A5_out(BYTE data) { cromemco_d7a_out(5, data); }
void cromemco_d7a_A6_out(BYTE data) { cromemco_d7a_out(6, data); }
void cromemco_d7a_A7_out(BYTE data) { cromemco_d7a_out(7, data); }

static BYTE cromemco_d7a_in(BYTE port)
{
#ifdef WANT_SDL
#ifdef HAS_NETSERVER
	if (n_flag)
		return inPort[port];
	else
#endif
		/* encode SDL input for D+A7 */
		switch(port) {
			case 0: return ~(sdl_joystick_0_buttons | (sdl_joystick_1_buttons << 4));
			case 1: return (sdl_joystick_0_x_axis / 256);
			case 2: return (-sdl_joystick_0_y_axis / 256);
			case 3: return (sdl_joystick_1_x_axis / 256);
			case 4: return (-sdl_joystick_1_y_axis / 256);
			default: return inPort[port];
		}
#else
	return inPort[port];	
#endif	/* WANT_SDL */
}

BYTE cromemco_d7a_D_in (void) { return cromemco_d7a_in(0); };
BYTE cromemco_d7a_A1_in(void) { return cromemco_d7a_in(1); };
BYTE cromemco_d7a_A2_in(void) { return cromemco_d7a_in(2); };
BYTE cromemco_d7a_A3_in(void) { return cromemco_d7a_in(3); };
BYTE cromemco_d7a_A4_in(void) { return cromemco_d7a_in(4); };
BYTE cromemco_d7a_A5_in(void) { return cromemco_d7a_in(5); };
BYTE cromemco_d7a_A6_in(void) { return cromemco_d7a_in(6); };
BYTE cromemco_d7a_A7_in(void) { return cromemco_d7a_in(7); };
