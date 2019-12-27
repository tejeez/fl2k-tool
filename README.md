# fl2k-tool
This repository contains some of my experiments on FL2000-based USB3-to-VGA
adapters, using the osmo-fl2k library [1] to use one as a general purpose
digital-to-analog converter.

Currently, there is only a simple WSPR [2] transmitter which was originally
written in a small software radio hackathon as an exercise on using the
library. A command-line interface for configuration was added later.

For example, to transmit on 160m, 80m and 40m, use this command with your
callsign, locator (2 letters and 2 numbers) and transmit power (in dBm)
filled in:

    ./fl-wspr f 1.8381e6 f 3.5701e6 f 7.0401e6 s $(./wspr_encode.py callsign locator dbm)


To increase transmit power, the R, G and B outputs can be all connected in
parallel, which should provide about 2.5 mW (0.7 Vpp) into a 25-ohm load.
An LC low-pass circuit can do both anti-alias filtering and impedance
matching, so that the output can be fed into an antenna.

# Licensing
Code here is licensed under GPL, since it depends on the GPL-licensed osmo-fl2k
library and SM0YSR's wspr-tools [3]. See COPYING.

# References for more information
* [1] https://osmocom.org/projects/osmo-fl2k/wiki/Osmo-fl2k
* [2] https://www.physics.princeton.edu/pulsar/K1JT/wspr.html
* [3] https://github.com/robertostling/wspr-tools
