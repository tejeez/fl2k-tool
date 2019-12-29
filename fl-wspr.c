/*
 * WSPR transmitter for FL2000-based USB3-VGA adapters
 *
 * Copyright (C) 2019 Tatu Peltola
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* The transmitter now does dithering to hopefully reduce quantization
 * spur levels. Some other possibilities to consider in future in order
 * to generate a cleaner signal:
 * - Amplitude ramps at start and end of transmission to avoid "key clicks"
 * - Noise shaping to push quantization noise away from the operating
 *   frequency, something similar to https://amcinnes.info/2012/uc_am_xmit/
 * - Interpolation of the sine table instead of phase dithering
 * - Try different sample rates and measure how it affects phase noise and
 *   spurs of the PLL that synthesizes the sample rate inside FL2000
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <osmo-fl2k.h>

#define FAIL(...) { fprintf(stderr, __VA_ARGS__); goto end; }
#define INFO(...) { fprintf(stderr, __VA_ARGS__); }

#define SINE_SHIFT 10
#define SINE_SIZE (1<<SINE_SHIFT)

#define WSPR_LEN 162
#define MAX_FREQS 16

struct configuration {
	uint32_t id;
	double fs, fs_exact, ppm, p1, p2;
	const char *s;
	char ps;
	unsigned nf;
	double f[MAX_FREQS];
};
#define CONFIGHELP \
"Configuration parameters:\n" \
"id   FL2K device ID\n" \
"fs   Target sample rate for FL2K (Hz)\n" \
"ppm  Frequency error of FL2K in parts per million\n" \
"s    WSPR symbols (string of 162 numbers between 0 and 3)\n" \
"f    WSPR center frequency (Hz)\n" \
"     To cycle between multiple bands, give multiple f parameters.\n" \
"p1   Phase shift for green channel (degrees)\n" \
"p2   Phase shift for blue channel (degrees)\n" \
"ps   Set to 1 to swap phase shifts of green and blue channel\n" \
"     before each transmission"


struct transmitter {
	double fs; // Exact sample rate
	char initialized, wspr_on, ps; // Flags
	int8_t *buf; // Buffer, allocated at init

	uint64_t phase, freq; // Oscillator phase and frequency
	uint64_t phs1, phs2; // Output phase shifts
	uint64_t lcg; // Linear congruential pseudorandom generator state

	uint64_t wspr_symphase;
	uint64_t wspr_freqs[MAX_FREQS], wspr_freq, wspr_step;
	uint32_t wspr_i; // WSPR symbol index being transmitted
	uint32_t wspr_nfreqs, wspr_freq_i;
	const char *wspr_data;
	int16_t sine[SINE_SIZE];
};

uint64_t tx_hz_to_freq(struct transmitter *tx, double hz)
{
	return hz / (double)tx->fs * ((double)(1ULL<<63) * 2.0);
}

void tx_init(struct transmitter *tx, struct configuration *conf)
{
	unsigned i;
	for (i = 0; i < SINE_SIZE; i++)
		tx->sine[i] = sin(6.283185307179586 * i / SINE_SIZE) * 0x7EFF;

	tx->fs = conf->fs_exact;
	tx->buf = malloc(FL2K_BUF_LEN * 3 * sizeof(&tx->buf));
	tx->wspr_on = 0;
	tx->wspr_data = conf->s;
	tx->wspr_step = tx_hz_to_freq(tx, 12000.0 / 8192);
	for (i = 0; i < conf->nf; i++)
		tx->wspr_freqs[i] = tx_hz_to_freq(tx, conf->f[i]);
	tx->wspr_nfreqs = conf->nf;
	tx->phs1 = conf->p1 * ((double)(1ULL<<63) / 180.0);
	tx->phs2 = conf->p2 * ((double)(1ULL<<63) / 180.0);
	tx->ps = conf->ps;
	tx->initialized = 1;
}

void tx_callback(fl2k_data_info_t *fldata)
{
	struct transmitter *tx = fldata->ctx;
	if (!tx->initialized)
		return;
	if (fldata->len != FL2K_BUF_LEN)
		return;

	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);
	if (!tx->wspr_on && (tp.tv_sec % 120) == 1) {
		// Start WSPR transmission
		tx->wspr_i = 0;
		tx->wspr_symphase = 0;
		tx->phase = 0;
		tx->wspr_freq = tx->wspr_freqs[tx->wspr_freq_i];
		tx->freq = tx->wspr_freq + tx->wspr_step * (tx->wspr_data[0] - '0');
		INFO("Starting WPSR transmission on band %d\n", tx->wspr_freq_i);
		tx->wspr_freq_i = (tx->wspr_freq_i + 1) % tx->wspr_nfreqs;
		if (tx->ps == 1) {
			uint64_t p = tx->phs1;
			tx->phs1 = tx->phs2;
			tx->phs2 = p;
		}
		tx->wspr_on = 1;
	}

	int8_t *b = tx->buf;
	long unsigned i;

	/* Copy most often used struct members to local variables */
	uint64_t tx_phase = tx->phase, tx_freq = tx->freq;
	uint64_t wspr_symphase = tx->wspr_symphase;
	uint64_t lcg = tx->lcg;
	char wspr_on = tx->wspr_on;
	const uint64_t wspr_step = tx->wspr_step;
	const uint64_t phs1 = tx->phs1;
	const uint64_t phs2 = tx->phs2;
	for (i = 0; i < FL2K_BUF_LEN; i++) {
		/* Pseudorandom generator for dithering, parameters from
		 * https://en.wikipedia.org/wiki/Linear_congruential_generator#Parameters_in_common_use */
		lcg = lcg * 6364136223846793005ULL + 1;
		uint32_t rnd = lcg >> 32;
		if (wspr_on) {
			tx_phase += tx_freq;
			/* Add phase dithering before truncation
			 * to sine table size */
			uint64_t ph = tx_phase + (rnd << (64-32-SINE_SHIFT));
			/* Outputs with different phase shifts */
			int16_t out0, out1, out2;
			out0 = tx->sine[ ph         >> (64-SINE_SHIFT)];
			out1 = tx->sine[(ph + phs1) >> (64-SINE_SHIFT)];
			out2 = tx->sine[(ph + phs2) >> (64-SINE_SHIFT)];
			/* Add dithering to output values.
			 * Use different bits of the RNG for each channel. */
			out0 += 0xFF & rnd;
			out1 += 0xFF & rnd >> 8;
			out2 += 0xFF & rnd >> 16;
			/* Quantization to 8 bits */
			b[0]              = (uint16_t)(0x7F00 + out0) >> 8;
			b[FL2K_BUF_LEN]   = (uint16_t)(0x7F00 + out1) >> 8;
			b[FL2K_BUF_LEN*2] = (uint16_t)(0x7F00 + out2) >> 8;
			b++;
			/* Next symbol when symphase wraps around */
			uint64_t sp = wspr_symphase;
			if ((wspr_symphase = sp + wspr_step) < sp) {
				if (++tx->wspr_i < WSPR_LEN) {
					unsigned s = tx->wspr_data[tx->wspr_i] - '0';
					tx_freq = tx->wspr_freq + tx->wspr_step * s;
					INFO("WSPR symbol %3u: %u\n", tx->wspr_i, s);
				} else {
					wspr_on = 0;
					INFO("Stopping WSPR transmission\n");
				}
			}
		} else {
			b[0]              =
			b[FL2K_BUF_LEN]   =
			b[FL2K_BUF_LEN*2] = 0x80;
			b++;
		}
	}
	tx->phase = tx_phase;
	tx->freq = tx_freq;
	tx->lcg = lcg;
	tx->wspr_symphase = wspr_symphase;
	tx->wspr_on = wspr_on;

	fldata->sampletype_signed = 0;
	fldata->r_buf = (char*)tx->buf;
	fldata->g_buf = (char*)tx->buf + FL2K_BUF_LEN;
	fldata->b_buf = (char*)tx->buf + FL2K_BUF_LEN*2;
}

volatile char running = 1;

void sighandler(int sig)
{
	(void)sig;
	running = 0;
}


int main(int argc, char *argv[])
{
	struct configuration conf1 = {
		.id = 0,
		.fs = 100e6,
		.ppm = 143.0,
		.nf = 0,
		.s = "",
		.p1 = 0,
		.p2 = 0,
		.ps = 0
	};
	struct transmitter tx1 = {
		.initialized = 0
	};
	struct configuration *conf = &conf1;
	struct transmitter *tx = &tx1;
	fl2k_dev_t *fl = NULL;
	int i;

	if (argc <= 1)
		FAIL(CONFIGHELP);
	for (i = 1; i < argc-1; i+=2) {
		char *p = argv[i], *v = argv[i+1];
		if (strcmp(p, "id") == 0)
			conf->id = atoi(v);
		else if (strcmp(p, "fs") == 0)
			conf->fs = atof(v);
		else if (strcmp(p, "ppm") == 0)
			conf->ppm = atof(v);
		else if (strcmp(p, "p1") == 0)
			conf->p1 = atof(v);
		else if (strcmp(p, "p2") == 0)
			conf->p2 = atof(v);
		else if (strcmp(p, "ps") == 0)
			conf->ps = atoi(v);
		else if (strcmp(p, "s") == 0)
			conf->s = v;
		else if (strcmp(p, "f") == 0) {
			if (conf->nf < MAX_FREQS) {
				conf->f[conf->nf] = atof(v);
				++conf->nf;
			}
		}
		else FAIL("Unknown configuration parameter %s\n", p);
	}
	i = strlen(conf->s);
	if (i != WSPR_LEN)
		FAIL("Please give %d symbols (%d given)\n", WSPR_LEN, i);
	if (conf->nf == 0)
		FAIL("Please give at least one center frequency\n");

	signal(SIGINT, sighandler);

	if (fl2k_open(&fl, conf->id) < 0)
		FAIL("Opening FL2K failed\n");

	/* The FL2K API is a bit strange:
	 * fl2k_start_tx has to be called before fl2k_set_sample_rate
	 * in order to work. */
	if (fl2k_start_tx(fl, tx_callback, tx, 2) < 0)
		FAIL("Starting FL2K transmission failed\n");

	if (fl2k_set_sample_rate(fl, (uint32_t)conf->fs) < 0)
		FAIL("Setting FL2K sample rate failed\n");

	uint32_t fs_r = fl2k_get_sample_rate(fl);
	conf->fs_exact = (1.0 + 1e-6 * conf->ppm) * fs_r;
	INFO("Reported exact sample rate: %lu, corrected: %.1f\n", (long unsigned)fs_r, conf->fs_exact);
	tx_init(tx, conf);

	INFO("Started transmitting\n");
	while (running)
		pause();
	INFO("Stopping transmitting\n");
	fl2k_stop_tx(fl);
end:
	if (fl != NULL) {
		INFO("Closing FL2K\n");
		fl2k_close(fl);
	}
	INFO("Exiting\n");
	return 0;
}
