TerraTec DMX 6Fire USB driver for ALSA (Linux 6.x and 7.x compatible)
===================================================================

This is the driver for the TerraTec DMX 6Fire USB. Current features include:
- Firmware uploading if device has been reconnected to power
- Playback and capture at 16, 24 and 32 bit, Samplerates 44100 to 192000,
  6/4 analog channels
- Playback at 16, 24 and 32 bit, Samplerates 44100 to 96000, 2 digital channels
  (no passthrough yet)
- Master volume in mixer
- Individual analog channel volumes in mixer
- Selection of line/phono capturing
- MIDI in/out
- Digital thru: device may act as converter coax <-> optical

What is yet missing:
- Digital input
- SPDIF passthrough
- pcm_mode=4 (see below)

Firmware Installation
----------------------
This driver requires proprietary firmware to function. A script is provided to automatically download and install it:

    $ chmod +x get_firmware.sh
    $ ./get_firmware.sh

Please reconnect your DMX 6Fire USB device after running the script.

Volume and Muting
------------------
After the device is plugged in the first time, depending on the sound
frontend you are using it may happen that no sound output occurs.

This modified driver initializes all channels unmuted and set to -10dB
to ensure audio works out of the box.

To adjust the hardware volume of these channels, please open a terminal and type:

$ aplay -l

This will show all devices that alsa is aware of. You will recognize
a string "card X" at the beginning of the line listing a device.
Remember this X and replace it in the following line:

$ alsamixer -c X

Here you have access to all voulme and mute controls and other settings
that can be changed on the device.

It is also possible to use a graphical mixer, there are several out there.

Module parameters
------------------
* tasklet_thresh
  possible values: non-negative integer
  default: 128

  Specifies the threshold, which causes a tasklet to be used to copy data
  between alsa and the device.

  0: deactivate this feature
  positive number: if the selected period size uses more or equal packets per
    urb than this number, a tasklet will be used to transfer data. Otherwise
    data is transferred immediately in the hardware interrupt.
    Remark: one packet maps to approximately 0.15 milliseconds.
  
  Using a tasklet means that the hardware interrupt routine will have to do less
  work and the OS is ready to serve other devices more quickly (keyword:
  "decreased DPC-Latency"). Data transfer (alsa <-> device) is scheduled later.
  This might lead to crackles, when this data transfer is delayed because of
  system activity. If you experience such problems, set this parameter to a
  higher value or disable the feature.
  
* pcm_mode
	possible values: 0-3
	default: 3
	
	Specifies the pcm mode the card will work at. The five modes are:
	0: no pcm at all (only midi and digital passthrough)
	1: analog only (all sample rates, including high sample rates)
	2: digital only (maximum sample rate: 96kHz)
	3: analog/digital mode, sample rates restricted to 96kHz
	   96kHz is the maximum sample rate that is allowed for digital audio.
	   If this mode is selected, analog and digital may be used in parallel
	   for all circumstances.
	4: analog/digital mode, sample rates not restricted (currently disabled)
	   In this mode an analog stream may be played back/captured at high sample
	   rates (176.4kHz and 192kHz). However, while an analog stream is open using
	   one of these sample rates, opening the digital audio device will fail.
  
Remarks
--------
Concerning firmware uploading: I removed the check that made firmware uploading
possible only for a specific device version. I don't known if firmware loading
will be successful on all devices. Reports from other users indicate that it
should work. If the device behaves strangely after a firmware upload has been
performed (every time the device has been reconnected to power), please report
this issue with a short description.

Since version 0.3.0, this driver can also be found in the linux kernel. This
page will be maintained in order for experimental things like digital in-/output
and latency optimization. If no problems are reported for a while here, the
version will go into the kernel.

Credits & Acknowledgements
---------------------------
This version is a restoration of the original driver by Torsten Schenk, updated to work with modern Linux Kernels.

*   **Code Restoration**: Ported to modern Linux Kernel APIs with the assistance of Gemini AI.
*   **Kernel Compatibility**: Supports the `hex_to_bin()` header layout used by
    Linux 6.x and 7.x. Linux 6.0 through 6.3 obtain the declaration from
    `<linux/kernel.h>`, while Linux 6.4 and newer include `<linux/hex.h>`
    explicitly.
*   **Testing & Firmware**: Firmware sourcing, verification, and hardware testing performed by the repository maintainer.

Tested Environment
-------------------
*   **OS**: Nobara Linux 43
*   **Hardware tested**: 6.18.7-200.nobara.fc43.x86_64
*   **Build tested**: 6.19.11-201.nobara.fc43.x86_64 and
    7.0.9-200.nobara.fc43.x86_64

**Important**: The stock `snd-usb-6fire` driver included in the upstream Linux
Kernel was observed to cause immediate system crashes (Kernel Panic) upon
firmware initialization on the hardware-tested kernel version. This updated
driver resolves these stability issues.
