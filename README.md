# z80pack

z80pack is a mature emulator of multiple platforms with 8080 and Z80 CPU.

This fork adds a couple of features to Udo Munk's original upstream project:
- support for S100 sound cards with SDL2 and PortAudio audio frameworks (currently supported Cromemco D+7A and ADS Noisemaker)
- joystick support (Cromemco D+7A) with common USB game controllers
- more accurate Cromemco Dazzler emulation (interlaced display, line status flag, window resize etc.)
- support for higher resolution S100 monochrome graphics (currently supported Vector Graphic High Resolution graphics board)
- build switches for Cromemco Dazzler and D+7A boards have been separated in order also to allow combinations with other sound hardware, see cromemcosim and imsaisim for an example implementation

## General notes/limitations:
- cromemcosim and imsaisim are used as examples how to enable and pre-configure the added hardware emulations
- Sound cards, joysticks or high resolution graphics currently works in command line mode only, not with the web frontend (Javascript library needs to be updated)
- be aware that implementations based on non-realtime multi-tasking OS like Windows or Linux will never achieve fully correct timing in emulations. The z80pack implementation tries to provide a roughly correct CPU clock in average over certain periods of time, but won't implement continuous timing accuracy, which in general limits applications which are based on tight timings with less than one millisecond accuracy

## Notes on Cromemco Dazzler
- define HAS_DAZZLER in the appropriate sim.h file to enable this emulation
- additional config settings in the system.conf file:
	- set **dazzler_interlaced** to 1 to enable interlaced display with correct 62 Hz field rate for the Dazzler
	- set **dazzler_line_sync** to 1 to enable more accurate timing for the Dazzler (also enables the even/odd line status flag)
	- set **dazzler_descrete_scale** to 1 if you prefer window sizing with full multiples of the pixel count

## Notes on Cromemco D+7A
- define HAS_D7A in the appropriate sim.h file to enable this emulation
- the D+7A now supports both audio playback and joystick inputs
- can be configured to produce a sound file as recording of an audio sequence
- build z80pack with WANT_SDL=YES to use SDL2 framework for display, joystick and sound (recommended)
- alternatively, build z80pack with WANT_PORTAUDIO=YES to use the PortAudio framework for audio only
- emulates two JS-1 joysticks with integrated speaker
- joystick 1 uses the lower 4 bits of port 24 for buttons input (pressed=0), port 25 for x-axis input and audio output, and port 26 for y-axis input
- joystick 2 uses the upper 4 bits of port 24 for buttons input (pressed=0), port 27 for x-axis input and audio output, and port 28 for y-axis input
- audio port 25 is mapped to the left stereo audio channel, audio port 27 is mapped to the right stereo audio channel
- additional config settings in the system.conf file:
	- set **d7a_sample_rate** as an integer for the sampling rate of the audio framework
	- set **d7a_sync_adjust** as a floating point number to adjust the sound buffer processing speed (to reach the optimum balance between buffer overflows and underflows)
	- set **d7a_buffer_size** as an integer for the size of the sample buffer (limits the processing delay)
  	- set **d7a_soundfile** as a string for the filename of the recording file (also enables recording)
  	- set **d7a_recording_limit** as an integer for the total number of samples to limit the size of a recording
  	- set **d7a_stats** to 1 for printing some audio stats when shutting down the emulator
 - audio and joystick/game controller support in Linux is not standardized, sometimes it works out of the box, sometimes it requires additional configurations. Especially game controller support might include rebuilding the kernel (this version of z80pack reports joysticks found during start-up of the emulation). Hint: check for files /dev/inputs/js* and /dev/inputs/event*. Also, passing through game controllers via WSL requires special handling.
 - some game controllers come with an integrated audio device, be aware that audio output might be routed to this device when being plugged in (no sound on all other devices). You might think about disabling the game controller's audio device.

## Notes on ADS Noisemaker
- define HAS_NOISEMAKER in the appropriate sim.h file to enable this emulation
- uses two AY-3-8910 programmed sound generators for stereo synthesis with 6 independent tone channels and 2 noise channels
- build z80pack with WANT_SDL=YES to use SDL2 framework for sound (recommended)
- alternatively, build z80pack with WANT_PORTAUDIO=YES to use the PortAudio framework for sound
- additional config settings in the system.conf file:
	- set **noisemaker_sample_rate** as an integer for the sampling rate of the audio framework
 	- set **noisemaker_soundfile** as a string for the filename of the recording file (also enables recording)
	- set **noisemaker_recording_limit** as an integer for the total number of samples to limit the size of a recording

## Notes on Vector Graphic HiRes Graphics
- define HAS_VECTOR_GRAPHIC_HIRES in the appropriate sim.h file to enable this emulation
- the Vector Graphic HiRes Graphics emulation uses a fixed window size (not resizable)
- additional config settings in the system.conf file:
	- set **vector_graphic_hires_mode** for the graphic mode either to "bilevel" or "halftone"
	- set **vector_graphic_hires_address** as an integer for the start address of the video buffer in memory
	- set **vector_graphic_hires_fg** as an tripe of R,G,B values between 0 and 255 for the foreground color (simulates a monochrome CRT display color)

Full documentation of the upstream project is at https://www.icl1900.co.uk/unix4fun/z80pack

In addition to Udo Munk's instructions on Ubuntu, below are the steps to bild z80pack also on Fedora Linux based on this repo.

## WSL notes
Since the later revisions, the use of z80pack under MS Windows is normally achieved via WSL. Passing through audio to the host hardware normally is not much of a problem, since with Windows 11, WSL passes audio via a separate PulseAudio server to the Windows audio system. For using USB game controllers within WSL, you have to bind and attach the game controller with usbipd-win to the running WSL instance.

There are, however, a couple of caveats. First, audio latency with WSL is significantly higher. This will not so much affect music playback, but sound events generated e.g. in games might not be in sync with the action. Second, the WSL Linux kernel probably will not be configured out-of-the-box for supporting game controllers. Consequently, you will have to change the kernel configuration, rebuild the kernel and activate it for WSL.

If a USB game controller also integrates audio hardware, your Linux in the WSL instance possibly automatically activates its own audio server such as PipeWire (if installed) for handling the newly detected audio hardware, which cuts the connection to WSL's PulseAudio server, so that all audio output will be routed to the game controller's audio hardware instead of the host's Windows audio system. You might need to deactivate the guest Linux' own audio server in order to re-establish the use of WSL's PulseAudio server.

## Fedora

1. Install the packages required for z80pack
```
sudo dnf group install development-tools
sudo dnf install git SDL2 SDL2-devel SDL2_mixer SDL2_mixer-devel SDL2_image SDL2_image-devel
```	
2. Get the latest z80pack sources
```
git clone https://github.com/akueckes/z80pack.git
```
3. Build z80pack
```
cd z80pack
WANT_SDL=YES make
```
4. Test the emulator e.g. with Cromemco emulation
```
cd cromemcosim
./cpm22
```

## Ubuntu 

### Building
First install the needed dependencies for X11:

    sudo apt install build-essential libglu1-mesa-dev libjpeg9-dev

or for SDL2:

    sudo apt install build-essential libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev

Then for X11 run  

    make

or for SDL2 run  

    WANT_SDL=YES make

to build all the MACHINES mentioned in the Makefile.  

## Release vs Development

Sometimes I get asked questions why something doesn't work, and this might
be caused by using an older version and not the latest current. Sometimes
people miss it, that the repository most of the time has two branches:

```
master - finished and final releases
dev - latest sources still under development, but usually stable
```

To use the latest dev version you need to do this:

```
git clone https://github.com/udo-munk/z80pack.git
cd z80pack
git checkout dev
```

You now will build everything from the latest dev branch and not some older
finished release.

### Running CP/M 2.2

CP/M 2.2 is the ancestor of MS-DOS. Use this command to invoke CP/M 2.2 with
two disks containing some sample programs and sources.


    (cd cpmsim; ./cpm22)

Use `DIR` to see files on disk. Exit again with `BYE`

Sample execution in WSL under Windows 10: 

```
#######  #####    ###            #####    ###   #     #
     #  #     #  #   #          #     #    #    ##   ##
    #   #     # #     #         #          #    # # # #
   #     #####  #     #  #####   #####     #    #  #  #
  #     #     # #     #               #    #    #     #
 #      #     #  #   #          #     #    #    #     #
#######  #####    ###            #####    ###   #     #

Release 1.38, Copyright (C) 1987-2024 by Udo Munk and others

CPU speed is unlimited, CPU executes undocumented instructions

Booting...

64K CP/M Vers. 2.2 (Z80 CBIOS V1.2 for Z80SIM, Copyright 1988-2007 by Udo Munk)

A>dir
A: DUMP     COM : SDIR     COM : SUBMIT   COM : ED       COM
A: STAT     COM : BYE      COM : RMAC     COM : CREF80   COM
A: LINK     COM : L80      COM : M80      COM : SID      COM
A: RESET    COM : WM       HLP : ZSID     COM : MAC      COM
A: TRACE    UTL : HIST     UTL : LIB80    COM : WM       COM
A: HIST     COM : DDT      COM : Z80ASM   COM : CLS      COM
A: SLRNK    COM : MOVCPM   COM : ASM      COM : LOAD     COM
A: XSUB     COM : LIB      COM : PIP      COM : SYSGEN   COM
A>dir B:
B: BOOT     HEX : BYE      ASM : CLS      MAC : SURVEY   MAC
B: R        ASM : CLS      COM : BOOT     Z80 : W        ASM
B: RESET    ASM : BYE      COM : SYSGEN   SUB : BIOS     HEX
B: CPM64    SYS : SPEED    C   : BIOS     Z80 : SPEED    COM
B: SURVEY   COM : R        COM : RESET    COM : W        COM
A>bye

System halted
CPU ran 3 ms and executed 1958078 t-states
Clock frequency 630.22 MHz
```

### Running CP/M 3

CP/M 3 was the next generation of CP/M with features from MP/M to notably
be able to use more RAM along with a lot of other nice features.  

Run with:

    (cd cpmsim; ./cpm3)

Sample run:

``` 
#######  #####    ###            #####    ###   #     #
     #  #     #  #   #          #     #    #    ##   ##
    #   #     # #     #         #          #    # # # #
   #     #####  #     #  #####   #####     #    #  #  #
  #     #     # #     #               #    #    #     #
 #      #     #  #   #          #     #    #    #     #
#######  #####    ###            #####    ###   #     #

Release 1.38, Copyright (C) 1987-2024 by Udo Munk and others

CPU speed is unlimited, CPU executes undocumented instructions

Booting...


LDRBIOS3 V1.2 for Z80SIM, Copyright 1989-2007 by Udo Munk

CP/M V3.0 Loader
Copyright (C) 1998, Caldera Inc.

 BNKBIOS3 SPR  FC00  0400
 BNKBIOS3 SPR  8600  3A00
 RESBDOS3 SPR  F600  0600
 BNKBDOS3 SPR  5800  2E00

 61K TPA

BANKED BIOS3 V1.6-HD, Copyright 1989-2015 by Udo Munk

A>setdef [no display]

Program Name Display - Off

A>setdef [uk]

Date format used     - UK

A>setdef *,a:,b:,i:

Drive Search Path:
1st Drive            - Default
2nd Drive            - A:
3rd Drive            - B:
4th Drive            - I:


A>setdef [order=(com,sub)]

Search Order         - COM, SUB

A>setdef [temporary=a]

Temporary Drive      - A:

A>hist

History RSX active
A>vt100dyn
(C) Alexandre MONTARON - 2015 - VT100DYN

RSX loaded and initialized.

Try

 A>DEVICE CONSOLE [PAGE]

to see if it works...

A>dir a:
A: CPM3     SYS : VT100DYN COM : TRACE    UTL : HIST     UTL : PROFILE  SUB
SYSTEM FILE(S) EXIST
A>dir b:
B: BNKBDOS3 SPR : CPM3     SYS : LDRBIOS3 MAC : SCB      MAC : RESBDOS3 SPR
B: BIOS3    MAC : PATCH    COM : GENCPM   COM : BDOS3    SPR : GENCPM   DAT
B: BOOT     Z80 : M80      COM : LINK     COM : L80      COM : WM       COM
B: MAC      COM : WM       HLP : BNKBIOS3 SPR : LDR      SUB : INITDIR  COM
B: CPMLDR   COM : COPYSYS  COM : CPMLDR   REL : RMAC     COM : SYSGEN   SUB
A>bye

System halted
CPU ran 14 ms and executed 10493728 t-states
Clock frequency 713.42 MHz
```

