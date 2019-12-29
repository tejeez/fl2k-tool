# fl2k-tool
This repository contains some of my experiments on FL2000-based USB3-to-VGA
adapters, using the osmo-fl2k library [1] to use one as a general purpose
digital-to-analog converter.

To install the software, first follow instructions in osmocom site [1] to
install the library and then run `make` in this directory. The software works
on Ubuntu Linux and probably other distributions as well, but other operating
systems haven't been tested yet.

# WSPR transmitter
Currently, there is only a simple WSPR [2] transmitter which was originally
written in a small software radio hackathon as an exercise on using the
library. A command-line interface for configuration and some additional
features were added later.

For example, to transmit on 160m, 80m and 40m, use a command like this:

    ./fl-wspr f 1.8381e6 f 3.5701e6 f 7.0401e6 ppm 140 s $(python3 wspr_encode.py CALL KP20 3)

Replace CALL with your callsign and KP20 with your Maidenhead locator.
Replace 3 with your transmit power in dBm (if using an amplifier).
Change the ppm value to correct for frequency error of your adapter.

To increase transmit power, the R, G and B outputs can be all connected in
parallel. This should provide about 2.5 mW (0.7 Vpp, about 3 dBm) into a
25-ohm load, decreasing on higher frequencies.
An LC low-pass circuit can do both anti-alias filtering and impedance
matching, so that the output can be fed into an antenna.

Phase shift between different outputs is also supported, so the program can
directly generate a balanced, I/Q or a three-phase signal. For example, to
generate 3-phase RF to feed a "tripole" antenna [3] for circular polarization
on 80m and swap handedness on each transmission, do:

    ./fl-wspr f 3.570123e6 p1 120 p2 240 ps 1 s $(python3 wspr_encode.py CALL KP20 3)

# Licensing
Code here is licensed under GPL, since it depends on the GPL-licensed osmo-fl2k
library and SM0YSR's wspr-tools [4]. See COPYING.

# References for more information
* [1] https://osmocom.org/projects/osmo-fl2k/wiki/Osmo-fl2k
* [2] https://www.physics.princeton.edu/pulsar/K1JT/wspr.html
* [3] https://73.fi/oh2aue/anti_fading_inverted_y_nvis_tripole_with_twisted_trilead_basic_block_diagram.gif
* [4] https://github.com/robertostling/wspr-tools
