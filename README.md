# fl2k-tool
This repository contains some of my experiments on FL2000-based USB3-to-VGA
adapters, using the osmo-fl2k library [1] to use one as a general purpose
digital-to-analog converter.

Currently, there is only a simple WSPR [2] transmitter which was written in one
evening in a small software radio hackathon as an exercise on using the
library. All the configuration is in the code now but a command-line interface
might be added next.

To increase transmit power, the R, G and B outputs can be all connected in
parallel, which should provide about 2.5 mW (0.7 Vpp) into a 25-ohm load.
An LC low-pass circuit can do both anti-alias filtering and impedance
matching, so that the output can be fed into an antenna.

# Licensing
Code here is licensed under GPL, since it depends on the GPL-licensed osmo-fl2k
library. See COPYING.

# References for more information
* [1] https://osmocom.org/projects/osmo-fl2k/wiki/Osmo-fl2k
* [2] https://www.physics.princeton.edu/pulsar/K1JT/wspr.html
