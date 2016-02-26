## mpdstate

mpdstate is a small C-program which outputs the state of
an MPD (Music Player Daemon). The program keeps waiting for changes
and outputs immediately the state changes that I found important.

The output is easily parsed with shell scripting, and that is how I automate
my MPD boxes to:

* enable a LED when MPD is playing
* activate the power amplifier's power when MPD is playing

## Example script

	#!/bin/sh
	
	mpdstate | while read PROP VALUE; do
	case "$PROP" in
	state)
		LEDVAL=0
		if [ "$VALUE" = "play" ]; then
			LEDVAL=255
		fi
		echo $LEDVAL > /sys/class/leds/myled/brightness
		;;
	esac

