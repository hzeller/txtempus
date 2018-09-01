Radio time station transmitter using the Raspberry Pi
=====================================================

I am living in a country where there is no [DCF77] sender nearby for my
European radio controlled wristwatch to get its time.
So I built my own 'transmitter', taking the [NTP] time of a Raspberry Pi and
generating a modulated radio signal via GPIO pins.

Since many other longwave time stations around the world use similar
concepts of sending amplitude modulated time, other time services have been
added.

This program is useful if you have a clock that otherwise does not get any
reception.
**_Before running this program, make sure you follow your local laws with
regard to restrictions on radio transmissions._**

### Supported Time Service Transmissions
#### DCF77
The [DCF77] (Germany) signal is a 77.5kHz carrier, that is amplitude modulated
with attenuations every second of the minute except the 59th to synchronize.
The length of the attentuation (100ms and 200ms) denotes bit values 0 and 1
respectively so in each minute, 59 bits can be transferred, containing
date and time information.

The Raspberry Pi has ways to create frequencies by integer division and
fractional jitter around that, which allows us to generate a frequency
of 77500.003Hz, which is close enough.

#### WWVB
The [WWVB] (USA) is on a 60kHz carrier, and also transmits one bit per second
with different attenuation times (200ms zero, 500ms one; 800ms sync) and
multiple synchronization bits.

#### JJY
The [JJY] (Japan) is similar to WWVB, with same timings of carrier switches,
but reversed power levels. Some bits are different. Two senders exist in Japan
with 40kHz and 60kHz carrier; their simulations can be choosen
with `YYT40` and `YYT60` in this program.

#### MSF
The [MSF] (United Kingdom) has yet another encoding, transferring two bits
per second (but only one of it contains information). Carrier is 60kHz.

### Minimal External Hardware

The external hardware is simple: we use the frequency output on one pin and
another pin to pull the signal to a lower level for the regular attenuation.

To operate, you need three resistors: 2x4.7kΩ and one 560Ω, wired to GPIO4 and
GPIO17 like so:

Schematic                      | Real world
-------------------------------|------------------------------
![](img/schematic-dcf77.png)   |![](img/contacts-dcf77.jpg)


GPIO4 and 17 are on the inner row of the Header pin, three pins inwards on
the [Raspberry Pi GPIO]-Header. You don't GPIO17 and the 560Ω resistor for `MSF`
transmission.

Now, wire a loop of wire between the open end of the one 4.7kΩ and  ground
(which is conveniently located between GPIO4 and GPIO17 and shown above with
the black heatshrink). Wrap this wire-loop roughly around the antenna of your
watch/clock or at least bring in close proximity:

![](img/watch-wired.jpg)

### Transmit!

```
 make
 sudo ./txtempus -v -s DCF77
```

With `-s`, you set the type of time signal you want to transmit.

There are a few options you can set. The `-r` option is useful to have the
program run only for the few minutes it might take for a clock to synchronize.

By default, the current system time is transmitted. The `-t` option allows
different times for testing.

```
usage: ./txtempus [options]
Options:
        -s <service>          : Service; one of 'DCF77', 'WWVB', 'JJY40', 'JJY60', 'MSF'
        -r <minutes>          : Run for limited number of minutes. (default: no limit)
        -t 'YYYY-MM-DD HH:MM' : Transmit the given local time (default: now)
        -v                    : Verbose.
        -n                    : Dryrun, only showing modulation envelope.
        -h                    : This help.
```

In the video below, you can see how a watch is set with this transmitter.
After it is set to zero, it waits until it sees the end-of-minute mark (which
does not have any amplitude modulation) and then starts to count on from
second 59, then gathering the data that is following.

An interesting observation: you see that the watch already gets into fully
set mode after about 50 seconds, even though there is the year data
after that. This particular watch never shows the year, so it just ignores that.

<p align="center"><a href="https://youtu.be/WzZnGimRj60">
  <img src="img/dcf77-video.jpg" width="50%"></a></p>

### Showing the modulation envelope

Mostly for debugging, the `-n` option allows to observe how the modulation
of each second looks like. You don't need to be root or run on the Raspberry
Pi to use this option.
Underscores show low power carrier, hashes high power:

```
$ ./txtempus -n -s wwvb
2018-08-17 13:22:00 -> tx-modulation
:00 [________##]
:01 [__########]
:02 [_____#####]
:03 [__########]
:04 [__########]
:05 [__########]
:06 [__########]
:07 [_____#####]
:08 [__########]
:09 [________##]
:10 [__########]
:11 [__########]
  ... and so on for the whole minute ...
```

### Limitations
In some of these protocols, there are additional bits that contain
information about upcoming daylight saving times, leap seconds or difference
to astronomic time. These are not set, but usually clocks are fine with it.

<hr/>

**tx** _common telecommunication abbreviation for 'transmit'_<br/>
**tempus**, n _Latin. Time; period; age_

[DCF77]: https://en.wikipedia.org/wiki/DCF77
[WWVB]: https://en.wikipedia.org/wiki/WWVB
[JJY]: https://en.wikipedia.org/wiki/JJY
[MSF]: https://en.wikipedia.org/wiki/Time_from_NPL_(MSF)
[NTP]: https://en.wikipedia.org/wiki/Network_Time_Protocol
[Raspberry Pi GPIO]: https://www.raspberrypi.org/documentation/usage/gpio/