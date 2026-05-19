/*
 * audio.c
 *
 * Audio functions for use in multiple device emulations
 *
 * Copyright (C) 2026 by Ansgar Kueckes
 *
 * History:
 * 19-APR-2026	1.0	Initial Release
 */
 
 /*
    This module implements an abstraction layer for using diverse audio
    subsystems within your audio device emulation.
    
    In general, two models are supported, one for devices using DACs for
    audio generation, and one for devices using programmable sound
    generators (PSGs).
    
    Examples for DAC based hardware are the Cromemco D+7A and the Newtech
    Model 6 Music Board. Later in history programmable sound generators
    such as the Solid State Music SB1 (also with discrete logic) or the
    ADS Noisemaker and the S-100 Sound Effects Board (both with a PSG on
    a chip) appeared.
    
    Digital Audio Converters (DACs)
    -------------------------------

    DACs need a continuous feed from the CPU, and for proper audio
    timing require to be in sync with the CPU emulation. Now in fact
    the emulation of the CPU happens in bursts of roughly 10 milliseconds,
    where realtime reference only is achieved across longer periods, but
    not as it would be required for synchronous data streams for continuous
    audio playback.
    
    Also, the rate samples are generated within a DAC type emulation normally
    won't match the sampling rate the audio subsystem is expecting.

    As a consequence, audio data provided by the CPU needs to be buffered
    with some reference to the CPU ticks associated with port output
    instructions, and re-sampled for the audio playback on the host.

    This buffered data then will be processed synchronously under control
    of the host's audio subsystem in order to generate a continuous audio
    stream with minimum glitches. In case the audio subsystem sampling rate
    is higher than the sampling rate which is feeding the DAC, intermediate
    samples need to be interpolated oversampling).
    
    A circular FIFO buffer for each channel is served by writing with
    fixed rate to the output ports. The FIFOs then work as sources for
    the audio stream, which is fed by a callback function being called each
    time the host's audio subsystem is ready for new data.
    
    Programmable Sound Generators (PSGs)
    ------------------------------------

    PSGs on the other side are processing audio data independent of the
    CPU, which frees up the CPU for doing other things while the PSG is
    generating the audio stream. The PSG emulation will be synchronized
    directly by the audio subsystem of the emulation host.

    Currently, with PortAudio and SDL2 Audio two common audio subsystems are
    supported, which again work as a frontend for a number of other low level
    sound frameworks, such as PulseAudio or ALSA.
    
    Sound recording
    ---------------

    The emulation can be configured to create a recording of the sound output
    during playback, which is provided in a WAV file after the emulation has
    stopped.
    
    Timing
    ------
    
    For appropriate sound generation, it is required to be 100% in sync
    with the emulator's CPU state clock.
    
    Recording a wave level from a specified audio port channel as realtime
    data into a circular FIFO buffer.

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
    the proper value for sync adjust in the system.conf file handled by
    your device emulation.
    
    Run the audio application of your choice, while changing that value until
    you achieve the best results for both underflows and overflows.
    
    Dropouts happen if the continuous data stream from the program running
    in the emulation doesn't write to the ports for a certain period of
    time, so that the audio output level does not change over a noticeable
    number of samples. This can be caused by the application program
    and/or related to the emulation of the 8080/Z80 CPU.

    TODOs
    -----

    - detect port inactivity for more intelligent silence handling
    
    Currently, no band pass filtering is performed, so digitalization
    artifacts are produced as with the original hardware.
 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simport.h"

#include "audio.h"

// #define LOG_LOCAL_LEVEL LOG_DEBUG
#include "log.h"
static const char *TAG = "audio";

#ifdef WANT_SDL
#include <SDL.h>
#include "simsdl.h"
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#ifdef WANT_PORTAUDIO
#include "portaudio.h"
#endif

#define MAX_NUM_DEVICES		8		/* maximum number of audio devices served */
#define NUM_CHANNELS 		2		/* number of channels, 2 for stereo */
#define FIFO_SIZE		4096		/* circular buffer size per channel */

typedef struct {
        char sample[FIFO_SIZE];  		/* actual circular FIFO buffer for samples */
        int tail;                		/* index to first entry in the FIFO buffer */
        int head;                        	/* index to next free buffer entry */
        int count;				/* number of samples in FIFO buffer */
} SampleBuffer;

typedef struct {
        int lock;				/* locks buffer changes during callback */
        int restart;				/* DAC is in restart phase */
	unsigned long frame_count;		/* how many frames have been streamed */
	unsigned long frame_offset;		/* offset between CPU ticks and frame count */
	unsigned long start_frame;		/* frame number when audio stream and CPU both become active */
	unsigned long start_time;		/* time when audio stream and CPU both become active */
	Tstates_t start_ticks;			/* CPU ticks when audio stream and CPU both become active */
	double sync_adjust;
	SampleBuffer channel[NUM_CHANNELS];	/* left audio channel */
} FIFO;

typedef struct _audiodevice {
    	unsigned long sample_rate;
    	unsigned long buffer_size;
    	double sync_adjust;
    	bool stats;
    	char *soundfile;
    	unsigned long recording_limit;
    	int16_t *wave_buffer[NUM_CHANNELS];
    	unsigned long wave_index;
    	FIFO buffer;				/* intermediate buffer for port data */
    	char last_data[NUM_CHANNELS];		/* current sample for each channel */
    	Tstates_t last_ticks[NUM_CHANNELS];	/* time stamp of last port command */
    	double timing_error[NUM_CHANNELS];	/* cumulated timing error in each channel */
    	Tstates_t dac_last_activity;
#ifdef WANT_SDL
	SDL_AudioDeviceID device_id;
#endif
#ifdef WANT_PORTAUDIO
    	PaStream *stream;
#endif
    	void (*device_audio_callback)(void *userdata, float *stream, int len);
    	void *device_audio_userdata;
} Audiodevice;

/* statistics */
static int underflows = 0;
static int overflows = 0;
static int dropouts = 0;
static int timeouts = 0;

/* global device objects */
static Audiodevice audio_devices[MAX_NUM_DEVICES];
static int num_audio_devices = 0;
static uint32_t used_devices = 0;

/*
 *	Feed audio data from a circular FIFO buffer into the host's audio subsystem
 */
static void dac_callback(void *userdata, float *stream, int frame_count)
{
    FIFO *buffer = &((Audiodevice *)userdata)->buffer;
    int sample_rate = ((Audiodevice*)userdata)->sample_rate;

    float *out = stream;
    int i = 0, max = 0, c;

    /* return if FIFO buffer is in use or no data is available */
    if (buffer->lock || (frame_count == 0) || (stream == NULL)) return;
    	
    /* register when both audio stream and CPU become active */
    if ((buffer->start_ticks == 0) && (T > 0)) {
    	buffer->start_ticks = T;
    	buffer->start_frame = buffer->frame_count;
    	buffer->start_time = get_clock_us();
    }
    	
    /* lock FIFO buffer */
    buffer->lock = 1;
    
    for (c=0; c<NUM_CHANNELS; c++)
	if (buffer->channel[c].count > max) max = buffer->channel[c].count;

    /* after restarting DAC activity, we need to fill the buffer at least for
       the ~100 ms CPU clock bursts in order to avoid dropouts */
    if (buffer->restart) {
    	if (max < (sample_rate / 10)) {
		/* clear audio buffer */
		memset(out, 0, frame_count * NUM_CHANNELS * sizeof(float));

		/* unlock FIFO buffer */
		buffer->lock = 0;
    		return;
    	}

    	/* finished with restart */
    	buffer->restart = 0;
    }
    
    /* copy FIFO buffer to audio stream */
    for(i=0; i<frame_count; i++) {
	for (c=0; c<NUM_CHANNELS; c++) {
	        if (buffer->channel[c].count > 0) {
	            /* copy sample to audio buffer */
		    *out++ = (float)buffer->channel[c].sample[buffer->channel[c].tail] / 128.0;
	            buffer->channel[c].count--;
	            buffer->channel[c].tail = (buffer->channel[c].tail + 1) % FIFO_SIZE;
		}
		else *out++ = 0;	/* skip sample/write out silence */
	}
    }

    /* unlock FIFO buffer */
    buffer->lock = 0;
    buffer->frame_count += frame_count;
}

#ifdef WANT_SDL

/*
 *	This routine will be called by the SDL2 audio framework each time new data
 *	for playback on an audio device is requested.
 */
static void sdl_audio_callback(void *userdata, uint8_t *stream, int len)
{
    Audiodevice *device = (Audiodevice *)userdata;
    int num_frames = len / (sizeof(float) * NUM_CHANNELS);
    float *out = (float *)stream;

    /* call device specific callback routine */
    device->device_audio_callback(device->device_audio_userdata, (float *)stream, num_frames);

    /* save into wave buffer */
    if (device->wave_index < device->recording_limit) {
	for (int i=0; i<num_frames; i++) {
		if (device->wave_index < device->recording_limit) {
			for (int c=0; c<NUM_CHANNELS; c++) {
				device->wave_buffer[c][device->wave_index] = (int16_t)(*out++ * 32767);
		        }
		        device->wave_index++;
		}
	}
    }
}

/*
 *	Creates a new SDL2 audio stream with its own callback function, being mixed together
 */
static SDL_AudioDeviceID sdl_audio_init(Audiodevice *device)
{
	SDL_AudioSpec desired, obtained;

	/* prepare audio properties for streaming */
	desired.freq = device->sample_rate;
	desired.format = AUDIO_F32SYS;			/* 32 bit floating point (altenatively use AUDIO_S8 or AUDIO_F32SYS) */
	desired.channels = NUM_CHANNELS;
	desired.samples = device->buffer_size;
	desired.padding = 0;
	desired.callback = sdl_audio_callback;
	desired.userdata = device;

	/* start streaming (actually, Mix_SetPostMix() should be used for not confusing applications using the SDL2 mixer) */
	device->device_id = SDL_OpenAudioDevice(0, 0, &desired, &obtained, 0);
	if (device->device_id == 0) {
		LOGE(TAG, "Failed to open SDL audio device: %s\n", SDL_GetError());
	}
	else {
		/* start audio device */
		SDL_PauseAudioDevice(device->device_id, 0);
	}

	/* get actual sample rate and buffer size */
	device->sample_rate = obtained.freq;
	device->buffer_size = obtained.samples;

	return device->device_id;
}

/*
 *	Terminates audio device
 */
static void sdl_audio_off(SDL_AudioDeviceID device_id)
{
	/* shutdown SDL audio  */
	SDL_CloseAudioDevice(device_id); 	
}

#endif	/* WANT_SDL */

#ifdef WANT_PORTAUDIO
/*
 *	This routine will be called by the PortAudio engine when audio is needed.
 *	It may called at interrupt level on some machines so don't do anything
 *	that could mess up the system like calling malloc() or free().
 */
static int paCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    /* unused parameters */
    (void) inputBuffer;
    (void) timeInfo;
    (void) statusFlags;
    
    Audiodevice *device = (Audiodevice *)userData;
    float *buffer = (float *)outputBuffer;

    /* call device specific callback routine */
    device->device_audio_callback(device->device_audio_userdata, outputBuffer, framesPerBuffer);

    /* save into wave buffer */
    if (device->wave_index < device->recording_limit) {
	for (unsigned long i=0; i<framesPerBuffer; i++) {
		if (device->wave_index < device->recording_limit) {
			for (int c=0; c<NUM_CHANNELS; c++) {
				device->wave_buffer[c][device->wave_index] = (int16_t)(*buffer++ * 32767);
		        }
		        device->wave_index++;
		}
	}
    }

    return 0;
}

/*
 *	Creates a new PortAudio audio stream with its own callback function, being mixed together
*/
static int portaudio_init(Audiodevice *device) {
    PaError err;
    const PaStreamInfo *stream_info;
    PaStreamParameters outputParameters;

    /* Initialize library before making any other calls. */
    err = Pa_Initialize();
    if( err != paNoError ) {
    	LOGE(TAG, "PortAudio: Could not initialize PortAudio\n");
    	goto error;
    }

    outputParameters.device                    = Pa_GetDefaultOutputDevice();
    outputParameters.channelCount              = NUM_CHANNELS;
    outputParameters.sampleFormat              = paFloat32;             /* 32 bit floating point output (paInt8 and paInt816 won't work) */
    outputParameters.suggestedLatency          = 0.2;			/* 200 ms */
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
                  &device->stream,
                  NULL, /* no input */
                  &outputParameters,
                  device->sample_rate,
                  device->buffer_size,
                  0, //paClipOff,
                  paCallback,
                  device);

    if( err != paNoError ) {
    	LOGE(TAG, "PortAudio: Could not open default stream\n");
    	goto error;
    }

    /* get actual sampling rate back */
    if ((stream_info = Pa_GetStreamInfo(&device->stream))) {
    	device->sample_rate = (unsigned long)stream_info->sampleRate;
    }
    
    err = Pa_StartStream(device->stream);
    if( err != paNoError ) {
    	LOGE(TAG, "\nPortAudio: Could not start stream\n");
    	goto error;
    }
        
    return 0;

error:
    Pa_Terminate();
    return -1;
}

/*
 *	Close PortAudio audio stream
 */
static void portaudio_off(PaStream *stream) {
    PaError err;

    err = Pa_StopStream(stream);
    if(err != paNoError) goto error;
    err = Pa_CloseStream(stream);
    if(err != paNoError) goto error;

    return;

error:
    Pa_Terminate();
    return;
}

#endif	/* WANT_PORTAUDIO */

/*
 *	Push signed 8-bit sample data onto audio FIFO
 */
static int push(SampleBuffer *channel, char data)
{
    int result = 0;

    if (channel->count == FIFO_SIZE) {
        channel->tail = (channel->tail + 1) % FIFO_SIZE;	/* buffer overflow */
        result = -1;
    } else {
        channel->count++;
    }

    channel->sample[channel->head] = data;
    channel->head = (channel->head + 1) % FIFO_SIZE;

    return result;
}

/*
 *	Process one signed 8-bit sample
 */
void audio_record(int device_handle, int c, char data)
{
    int i, count = 0;
    uint64_t timeout, target;
    Tstates_t current_ticks;
    double slope, current_level, ratio, diff;
    
    Audiodevice *device = &audio_devices[device_handle];
    FIFO *buffer = &device->buffer;
    
    /* save current CPU state clock */
    current_ticks = T;
    
    /* every inactivity > 10 ms is considered as pausing audio, requiring a reset */
    if ((current_ticks - device->dac_last_activity) > ((unsigned long)f_value * 1000)) {
    	/* reset buffer */
	buffer->restart = 1;
    	for (i=0; i<NUM_CHANNELS; i++) {
    		device->last_ticks[i] = current_ticks;
		device->timing_error[i] = 0.0;
		buffer->channel[i].tail = 0;
		buffer->channel[i].head = 0;
		buffer->channel[i].count = 0;
    	}
    };
    device->dac_last_activity = current_ticks;
    
    /* sync CPU ticks with audio stream */
    target = (current_ticks - buffer->start_ticks) * device->sample_rate / (f_value * 1000000);
    target += buffer->frame_offset;
    target = target * buffer->sync_adjust;

    /* if required, re-sync with audio stream */
    if (target < buffer->frame_count) {
    	if (device->sync_adjust == 0.0)
    		buffer->sync_adjust = (double)(get_clock_us() - buffer->start_time) / (current_ticks - buffer->start_ticks) * f_value;
    	buffer->frame_offset = buffer->frame_count - target + device->buffer_size;
    }

    /* create a (tunable) timing reference sample rate vs nominal emulated CPU clock */
    ratio = device->sample_rate / (f_value * 1000000.0) * buffer->sync_adjust;

    /* calculate the number of samples since the last port write */
    if (device->last_ticks[c] == 0) device->last_ticks[c] = current_ticks;
    diff = (current_ticks - device->last_ticks[c]) * ratio;
    count = diff;					/* get integer */
    device->timing_error[c] += diff - count;		/* add fraction to error */
    if (device->timing_error[c] >= 1.0) {
      	count++;
       	device->timing_error[c] -= 1.0;
    }
    device->last_ticks[c] = current_ticks;
    
#ifdef WANT_SDL
    SDL_LockAudioDevice(device->device_id);
    UNUSED(timeout);
#else
    /* wait for being unlocked */
    timeout = 1000000;
    while(buffer->lock && !timeout) timeout--;
    if (!timeout) {
	timeouts++;
    	return;
    }
    buffer->lock = 1;
#endif

    if (buffer->channel[c].count == 0) underflows++;

    if (count > (FIFO_SIZE - buffer->channel[c].count)) {
        /* drop new data */
        count = FIFO_SIZE - buffer->channel[c].count;
        overflows++;
    }
    else if (count > 5) {
        dropouts++;
    }

    /* append to FIFO buffer (time interpolated) */
    if (count == 1) {
        /* append single sample to FIFO buffer */
        push(&buffer->channel[c], data);
    }
    else if (count < 5) {
        /* apppend group of samples (interpolated) to FIFO buffer */
        current_level = (double)device->last_data[c];
        slope = ((double)data - (double)device->last_data[c]) / (double)count;
        for (i=0; i<count; i++) {
	        push(&buffer->channel[c], data);
	        current_level += slope;	
        }
    }
    else {
        /* append silence to FIFO buffer */
        for (i=0; i<count; i++) {
		push(&buffer->channel[c], data);
    	}
    }
    
#ifdef WANT_SDL
    SDL_UnlockAudioDevice(device->device_id);
#else
    buffer->lock = 0;
#endif
    
    device->last_data[c] = data;
}

/*
 *	Open new audio device
 *	
 *	In case of success returns a handle to the new audio instance,
 *	or -1 in case of error
 */
int audio_init(
    	unsigned long *sample_rate,	/* requested ampling rate, returns actual sampling rate */
    	unsigned long *buffer_size,	/* requested buffer size, returns actual buffer size */
    	double sync_adjust,
    	bool stats,
    	char *soundfile,
    	unsigned long recording_limit,
    	void *callback,
    	void *userdata)
{
	int device_handle, c;
	Audiodevice *device;
	FIFO *buffer;
	
	if (num_audio_devices >= MAX_NUM_DEVICES) {
		LOGE(TAG, "Failed to register device (max. %d audio devices supported)\n", MAX_NUM_DEVICES);
		return -1;
	}
	
	for (device_handle=0; device_handle<MAX_NUM_DEVICES; device_handle++) {
		if ((used_devices & (1 << device_handle)) == 0) break;
	}
	device = &audio_devices[device_handle];
	buffer = &device->buffer;

	device->sample_rate = *sample_rate;
	device->buffer_size = *buffer_size;
	device->sync_adjust = sync_adjust;
	device->stats = stats;
	device->soundfile = soundfile;
	device->recording_limit = recording_limit;

	if (callback) {
		/* use user provided callback function */
		device->device_audio_callback = callback;
	} else {
		/* use integrated audio DAC as default */
		device->device_audio_callback = &dac_callback;
	}

	if (userdata) {
		/* use user provided user data */
		device->device_audio_userdata = userdata;
	} else {
		/* use device as default */
		device->device_audio_userdata = device;
	}

	/* setup wave buffer for recording */
	device->dac_last_activity = 0;
	device->wave_index = 0;
	if (device->recording_limit > 0) {
		for (c=0; c<NUM_CHANNELS; c++) {
			device->wave_buffer[c] = malloc(device->recording_limit * sizeof(int16_t));
			if (device->wave_buffer[c] == NULL) {
				LOGE(TAG, "Could not allocate enough memory for wave buffer, please reduce recording limit\n");
			}
		}
	};

	/* setup FIFO buffer for DAC */
	buffer->restart = 0;
	buffer->frame_count = 0;
	buffer->frame_offset = 0;
	buffer->start_ticks = 0;
	buffer->start_frame = 0;
	buffer->start_time = 0;
	buffer->sync_adjust = 1.0;

    	if (device->sync_adjust != 0.0)
    		buffer->sync_adjust = device->sync_adjust;
    
	for (c=0; c<NUM_CHANNELS; c++) {
		device->last_ticks[c] = 0;
		device->timing_error[c] = 0.0;
		buffer->channel[c].tail = 0;
		buffer->channel[c].head = 0;
		buffer->channel[c].count = 0;
		memset(buffer->channel[c].sample, 0, FIFO_SIZE);
	}
	
#ifdef WANT_SDL
    if ((device->device_id = sdl_audio_init(device)) == 0) {
    	    LOG(TAG, "Could not initialize SDL audio\n");
    	    return -1;
    }
#endif

#ifdef WANT_PORTAUDIO
    /* initialize PortAudio */
    if (portaudio_init(device) < 0) {
    	    LOGE(TAG, "Could not initialize PortAudio\n");
    	    return -1;
    };
#endif

    num_audio_devices++;
    used_devices |= 1 << device_handle;

    return device_handle;
}

/*
 *	Close the audio device
 */
void audio_off(int device_handle)
{
    int16_t buf[2048];
    int c, buf_index, data_size;
    unsigned long i;
    Audiodevice *device;
    
    if (device_handle > MAX_NUM_DEVICES) {
    	LOGE(TAG, "Device handle out of range\n");
    	return;
    }
    
    if ((used_devices & (1 << device_handle)) == 0) {
    	LOGE(TAG, "Unused device handle\n");
    	return;
    }

    device = &audio_devices[device_handle];

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
    
    if (device->soundfile && (device->wave_index > 0)) {

	/* open wave file */
	FILE *fp = fopen(device->soundfile, "wb");
	if (!fp) {
	    LOGE(TAG, "Couldn't open file %s\n", device->soundfile);
	    return;
	}
	else {
		data_size = device->wave_index * 2 * NUM_CHANNELS;
		
		/* setup wave header */
		memcpy(header.chunk_id, "RIFF", 4);
		header.chunk_size = data_size + 40;
		memcpy(header.format, "WAVE", 4);
		memcpy(header.subchunk1_id, "fmt ", 4);
		header.subchunk1_size = 16;
		header.audio_format = 1;
		header.num_channels = NUM_CHANNELS;
		header.sample_rate = device->sample_rate;
		header.byte_rate = device->sample_rate * 2 * NUM_CHANNELS;
		header.block_align = 4;
		header.bits_per_sample = 16;
		memcpy(header.subchunk2_id, "data", 4);
		header.subchunk2_size = data_size;
		
		/* write wave header */
		fwrite(&header, sizeof(header), 1, fp);
		
		/* write sample data */
		buf_index = 0;
		for (i=0; i<device->wave_index; i++) {
			for (c=0; c<NUM_CHANNELS; c++) {
				buf[buf_index++] = device->wave_buffer[c][i];
			}
			if (buf_index == 1024) {
		        	fwrite(buf, 1024, 2, fp);
		        	buf_index = 0;
			}
		}
		if (buf_index > 0) fwrite(buf, buf_index, 2, fp);
	
	    	fclose(fp);
	}
    }

    if (device->stats) {
	LOG(TAG, "Audio DAC: underflows: %d overflows: %d dropouts: %d timeouts: %d\n",
		underflows, overflows, dropouts, timeouts);
    }
    
#ifdef WANT_SDL
    sdl_audio_off(device->device_id);
#endif

#ifdef WANT_PORTAUDIO
    portaudio_off(device->stream);
#endif

    num_audio_devices--;
    used_devices &= ~(1 << device_handle);

    for (c=0; c<NUM_CHANNELS; c++) {
	if (device->wave_buffer[c]) free(device->wave_buffer[c]);
    }
}

#if 0
/*
 *	Return ratio of real time vs. emulated CPU ticks
 */
double audio_calibrate(int calibration_time)
{
    uint64_t start_time, end_time;
    Tstates_t T_start;
    
    T_start = T;
    start_time = get_clock_us();
    end_time = start_time + calibration_time * 1000000;
    
    /* calibrate over (roughly) calibration_time */
    while(get_clock_us() < end_time) sleep_for_ms(1);
    
    return (get_clock_us() - start_time) / (T - T_start);
}
#endif