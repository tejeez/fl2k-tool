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
	double fs;
	double fs_exact;
	double ppm;
	double f[MAX_FREQS];
	unsigned nf;
	const char *s;
};
#define CONFIGHELP \
"Configuration parameters:\n" \
"id   FL2K device ID\n" \
"fs   Target sample rate for FL2K (Hz)\n" \
"ppm  Frequency error of FL2K in parts per million\n" \
"s    WSPR symbols (string of 162 numbers between 0 and 3)\n" \
"f    WSPR center frequency (Hz)\n" \
"     To cycle between multiple bands, give multiple f parameters.\n"
;

struct transmitter {
	double fs; // Exact sample rate
	char initialized, wspr_on; // Flags
	int8_t *buf; // Buffer, allocated at init

	uint64_t phase, freq; // Oscillator phase and frequency

	uint64_t wspr_symphase;
	uint64_t wspr_freqs[MAX_FREQS], wspr_freq, wspr_step;
	uint32_t wspr_i; // WSPR symbol index being transmitted
	uint32_t wspr_nfreqs, wspr_freq_i;
	const char *wspr_data;
	int8_t sine[SINE_SIZE];
};

uint64_t tx_hz_to_freq(struct transmitter *tx, double hz)
{
	return hz / (double)tx->fs * ((double)(1ULL<<63) * 2.0);
}

void tx_init(struct transmitter *tx, struct configuration *conf)
{
	unsigned i;
	for (i = 0; i < SINE_SIZE; i++) {
		tx->sine[i] = 127.0 * sin(6.283185307179586 * i / SINE_SIZE);
	}

	tx->fs = conf->fs_exact;
	tx->buf = malloc(FL2K_BUF_LEN * sizeof(&tx->buf));
	tx->wspr_on = 0;
	tx->wspr_data = conf->s;
	tx->wspr_step = tx_hz_to_freq(tx, 12000.0 / 8192);
	for (i = 0; i < conf->nf; i++)
		tx->wspr_freqs[i] = tx_hz_to_freq(tx, conf->f[i]);
	tx->wspr_nfreqs = conf->nf;
	tx->initialized = 1;
}

void tx_callback(fl2k_data_info_t *fldata)
{
	struct transmitter *tx = fldata->ctx;
	if (!tx->initialized)
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
		tx->wspr_on = 1;
	}

	int8_t *b = tx->buf;
	long unsigned i, bl = fldata->len;

	for (i = 0; i < bl; i++) {
		if (tx->wspr_on) {
			tx->phase += tx->freq;
			*b++ = tx->sine[tx->phase >> (64-SINE_SHIFT)];
			/* Next symbol when symphase wraps around */
			uint64_t sp = tx->wspr_symphase;
			if ((tx->wspr_symphase = sp + tx->wspr_step) < sp) {
				if (++tx->wspr_i < WSPR_LEN) {
					unsigned s = tx->wspr_data[tx->wspr_i] - '0';
					tx->freq = tx->wspr_freq + tx->wspr_step * s;
					INFO("WSPR symbol %3u: %u\n", tx->wspr_i, s);
				} else {
					tx->wspr_on = 0;
					INFO("Stopping WSPR transmission\n");
				}
			}
		} else {
			*b++ = 0;
		}
	}

	fldata->sampletype_signed = 1;
	fldata->r_buf = (char*)tx->buf;
	fldata->g_buf = (char*)tx->buf;
	fldata->b_buf = (char*)tx->buf;
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
		.s = ""
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
