# z80pack

z80pack is a mature emulator of multiple platforms with 8080 and Z80 CPU.

This fork adds a couple of features to Udo Munk's original upstream project:
- support for S100 sound cards with SDL2 and PortAudio sound frameworks (currently used by Cromemco D+7A and ADS Noisemaker emulation)
- joystick support (Cromemco D+7A) with common game controllers
- more accurate Cromemco Dazzler emulation (performance rendering, interlaced display, line status flag, window resize etc.)
- support for higher resolution S100 monochrome graphics (Vector Graphic High Resolution Graphics board)
- Cromemco Dazzler and D+7A boards now can be individually selected via separate build switches
- bugfix for Cromemco Cyclops emulation
- more general structure for SDL2 clients (can also be windowless)

## General notes/limitations:
- machines cromemcosim and imsaisim are used as examples how to enable and pre-configure the new hardware emulations (see sim.h, simio.c, simcfg.c and system.conf files)
- sound cards, joysticks and high resolution graphics currently operate in command line mode only, the z80pack web frontend is missing the appropriate Javascript support (library needs to be updated). Also, there is a bug in the Javascript library which affects the correct rendering of 4x4 (=monochrome) video mode for the Dazzler emulation.
- be aware that implementations based on non-realtime multi-tasking OS like Windows or Linux will never achieve fully correct timing in emulations. The z80pack core implementation tries to provide in average a roughly correct CPU clock, but doesn't implement continuous timing accuracy.
- SDL2 under certain conditions seems to create noise with a phase directly related to the selected audio buffer size, probably when calling the audio callback function. Only seen with Ubuntu (libsdl2-2.0-0 version 2.30.0+dfsg-1ubuntu3.1), no fix yet.

## Notes on Cromemco Dazzler
- define HAS_DAZZLER in the appropriate sim.h file to enable this emulation
- additional config settings in the system.conf file:
	- set **dazzler_interlaced** to 1 to enable interlaced display for the Dazzler
	- set **dazzler_line_sync** to 1 for enabling the odd-even-line status flag
	- set **dazzler_discrete_scale** to 1 if you prefer window sizing with full multiples of the pixel count

## Notes on Cromemco D+7A
- define HAS_D7A in the appropriate sim.h file to enable this emulation
- the D+7A now supports both audio playback and joystick inputs
- can be configured to produce a sound file as recording of an audio sequence
- build z80pack with WANT_PORTAUDIO=YES to use the PortAudioframework for sound, X11 for display and the standard Linux joystick driver for joysticks
- alternatively, build z80pack with WANT_SDL=YES to use SDL2 framework for display, joysticks and sound
- emulates two JS-1 joysticks with integrated speaker (assigned to left and right audio channel, respectively)
- joystick 1 uses the lower 4 bits of port 24 for buttons input (pressed=0, released=1), port 25 for x-axis input and audio output, and port 26 for y-axis input
- joystick 2 uses the upper 4 bits of port 24 for buttons input (pressed=0, released=1), port 27 for x-axis input and audio output, and port 28 for y-axis input
- audio port 25 is mapped to the left audio channel, audio port 27 is mapped to the right audio channel
- additional config settings in the system.conf file:
	- set **d7a_sample_rate** as an integer for the sampling rate of the audio framework in samples/second
	- set **d7a_sync_adjust** as a floating point number to adjust the sound buffer processing speed with a multiplier (in order to reach the optimum balance between buffer overflows and underflows), where a value of 1.0 means no change
	- set **d7a_buffer_size** as an integer for the size of the sample buffer in samples (limits the processing delay)
  	- set **d7a_soundfile** as a string for the filename of the recording file (also enables recording)
  	- set **d7a_recording_limit** as an integer for the maximum number of samples to limit the size of a recording
  	- set **d7a_stats** to 1 for printing some audio stats when shutting down the emulator (can be used for tuning the balance of buffer over- and underflows)
 - audio and joystick/game controller support in Linux is not standardized, sometimes it works out of the box, sometimes it requires additional configurations. Especially game controller support might include rebuilding the kernel (this version of z80pack reports joysticks found during start-up of the emulation). Hint: check for files /dev/inputs/js* and /dev/inputs/event*. Also, passing through game controllers via WSL requires special handling.
 - some game controllers come with an integrated audio device, be aware that audio output might be routed to this device when being plugged in (no sound on all other devices). You might think about disabling the game controller's audio device.

## Notes on ADS Noisemaker
- define HAS_NOISEMAKER in the appropriate sim.h file to enable this emulation
- uses two AY-3-8910 programmed sound generators for stereo synthesis with 6 independent tone channels and 2 noise channels
- build z80pack with WANT_PORTAUDIO=YES to use the PortAudioframework for sound, X11 for display and the standard Linux joystick driver for joysticks
- alternatively, build z80pack with WANT_SDL=YES to use SDL2 framework for display, joysticks and sound
- additional config settings in the system.conf file:
	- set **noisemaker_sample_rate** as an integer for the sampling rate of the audio framework in samples/second
 	- set **noisemaker_soundfile** as a string for the filename of the recording file (also enables recording)
	- set **noisemaker_recording_limit** as an integer for the maximum number of samples to limit the size of a recording

## Notes on Vector Graphic HiRes Graphics
- define HAS_VECTOR_GRAPHIC_HIRES in the appropriate sim.h file to enable this emulation
- additional config settings in the system.conf file:
	- set **vector_graphic_hires_mode** for the graphic mode either to "bilevel" or "halftone"
	- set **vector_graphic_hires_address** as an integer for the start address of the video buffer in memory
	- set **vector_graphic_hires_fg** as a triple of R,G,B values each between 0 and 255 for the foreground color (simulates a monochrome CRT display color)


Full documentation of the upstream project is at https://www.icl1900.co.uk/unix4fun/z80pack

In addition to Udo Munk's instructions on Ubuntu, below are the steps to bild z80pack also on Fedora Linux based on this repo.

## PortAudio notes
The PortAudio sound framework can be selected instead of SDL2 for emulation of devices which support audio (e.g. Cromemco D+7A and ADS Noisemaker emulation). First make sure the proper packages are installed (portaudio-devel for Fedora, and portaudio19-dev for Debian/Ubuntu). Then use 'WANT_PORTAUDIO=YES make' for building z80pack with support for the PortAudio framework. For using Windows Subsystem for Linux (WSL) there may be additional steps required (see WSL section below).

## WSL notes
Since the later revisions, the use of z80pack under MS Windows is normally achieved via WSL2. Since Windows 11, WSL passes audio via an intermediate PulseAudio server to the Windows audio system. With SDL2, this should not be a problem. For PortAudio, most ready-to-use PortAudio packages do NOT support PulseAudio out of the box, since PulseAudio has been replaced by ALSA + PipeWire in recent Linux distros. In that case you will get a "PortAudio: Could not open default stream" error message (among others). The solution is to re-build the PortAudio package with enabling PulseAudio support:
```
git clone https://github.com/PortAudio/portaudio.git
cd portaudio
./configure --with-pulseaudio=yes --with-alsa=no
make
sudo make install
```
Reboot to make the changes effective.

WSL cannot pass through bluetooth controllers to Linux. For using USB game controllers within WSL, you have to bind and attach the game controller with usbipd-win to the running WSL instance. Plug in your joystick, open a Windows command shell, and list the available USB devices with
```
usbipd list
```
Your game controller should be listed with its bus-id under "Connected:". Then bind & attach the device to the running WSL instance with
```
usbipd bind --busid <bus-id>
usbipd attach --wsl --busid <bus-id>
```
Check with 'lsusb' in the WSL console, whether the device gets listed.

There are, however, a couple of caveats. First, audio latency with WSL is significantly higher. This will not so much affect music playback, but sound events generated e.g. in games might not be in sync with the action. Second, the WSL Linux kernel probably will not be configured out-of-the-box for supporting game controllers. You might have to change the kernel configuration, rebuild the kernel and activate it for WSL.

If a USB game controller also integrates audio hardware, your Linux in the WSL instance possibly automatically activates its own audio server such as PipeWire (if installed) for handling the newly detected audio hardware, which cuts the connection to WSL's PulseAudio server, so that all audio output will be routed to the game controller's audio hardware instead of the host's Windows audio system. You might need to deactivate the guest Linux' own audio server in order to re-establish the use of WSL's PulseAudio server.

In general, the X server integrated in WSL2 (WSLg) is special in certain aspects. Window size hints seem to be widely ignored by WSL's window manager. You might consider using GWSL (https://opticos.github.io/gwsl/) instead.

## Fedora

1. Install the packages required for z80pack
```
sudo dnf group install development-tools
sudo apt install mesa-libGL-devel libjpeg-devel libXrender-devel
```
For SDL2, also install
```
sudo dnf install SDL2 SDL2-devel SDL2_mixer SDL2_mixer-devel SDL2_image SDL2_image-devel
```	
2. Get the latest z80pack sources
```
git clone https://github.com/akueckes/z80pack.git
```
3. Build z80pack for X11 and PortAudio with
```
cd z80pack
WANT_PORTAUDIO=YES make
```
or for SDL2 with
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

    sudo apt install build-essential libglu1-mesa-dev libjpeg9-dev libxrender-dev

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

