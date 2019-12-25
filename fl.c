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
#include <osmo-fl2k.h>

#define FAIL(...) { fprintf(stderr, __VA_ARGS__); goto end; }
#define INFO(...) { fprintf(stderr, __VA_ARGS__); }

#define SINE_SHIFT 10
#define SINE_SIZE (1<<SINE_SHIFT)

#define WSPR_LEN 162

// Generated using https://github.com/robertostling/wspr-tools
const uint8_t wspr_oh2ehk[WSPR_LEN] =
{3,1,0,2,2,0,0,0,3,0,0,2,1,3,1,2,0,2,1,0,2,3,0,3,1,3,3,2,0,2,2,0,2,0,3,0,0,1,2,1,2,0,0,2,0,0,1,2,3,1,2,2,3,3,2,1,0,2,2,3,3,2,3,2,0,0,2,3,1,0,3,2,1,2,1,2,1,2,0,1,2,0,3,2,3,1,2,0,2,1,3,0,1,0,1,0,0,2,3,2,0,0,2,0,1,0,0,3,0,2,1,1,3,2,1,1,0,0,3,3,0,1,0,0,0,1,1,3,2,2,0,0,2,3,2,1,0,0,3,1,0,0,0,0,2,2,0,1,1,0,3,0,1,1,0,0,0,1,1,0,2,0}
;

struct transmitter {
	double fs; // Sample rate
	char initialized, wspr_on; // Flags
	int8_t *buf; // Buffer, allocated at init

	uint64_t phase, freq; // Oscillator phase and frequency

	uint32_t wspr_i; // WSPR symbol index being transmitted
	uint32_t wspr_symclock, wspr_symperiod;
	uint64_t wspr_freqs[4];
	//uint8_t wspr_data[WSPR_LEN];
	const uint8_t *wspr_data;
	int8_t sine[SINE_SIZE];
};

uint64_t tx_hz_to_freq(struct transmitter *tx, double hz)
{
	return hz / (double)tx->fs * ((double)(1ULL<<63) * 2.0);
}

void tx_init(struct transmitter *tx)
{
	double wspr_hz = 7.0401e6;
	int i;
	for (i = 0; i < SINE_SIZE; i++) {
		tx->sine[i] = 127.0 * sin(6.283185307179586 * i / SINE_SIZE);
	}

	// tx->fs is set by main
	tx->buf = malloc(FL2K_BUF_LEN * sizeof(&tx->buf));
	tx->wspr_on = 0;
	tx->wspr_symperiod = tx->fs / 1.46;
	tx->wspr_data = wspr_oh2ehk;

	for (i = 0; i < 4; i++) {
		tx->wspr_freqs[i] = tx_hz_to_freq(tx, wspr_hz + 1.46 * i);
	}

	//INFO("TX freq: %llu\n", (long long unsigned)tx->freq);
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
		tx->wspr_symclock = 0;
		tx->phase = 0;
		tx->freq = tx->wspr_freqs[tx->wspr_data[0]];
		tx->wspr_on = 1;
		INFO("Starting WPSR transmission\n");
	}

	int8_t *b = tx->buf;
	long unsigned i, bl = fldata->len;

	for (i = 0; i < bl; i++) {
		if (tx->wspr_on) {
			tx->phase += tx->freq;
			*b++ = tx->sine[tx->phase >> (64-SINE_SHIFT)];
			if (++tx->wspr_symclock >= tx->wspr_symperiod) {
				tx->wspr_symclock = 0;
				if (++tx->wspr_i < WSPR_LEN) {
					unsigned s = tx->wspr_data[tx->wspr_i];
					tx->freq = tx->wspr_freqs[s];
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

char running = 1;

void sighandler(int sig)
{
	(void)sig;
	running = 0;
}

int main()
{
	double freq_calib = 1.00011;
	uint32_t fs_target = 100e6;
	struct transmitter tx = { 0 };
	fl2k_dev_t *fl = NULL;

	signal(SIGINT, sighandler);

	if (fl2k_open(&fl, 0) < 0)
		FAIL("Opening FL2K failed\n");

	/* The FL2K API is a bit strange:
	 * fl2k_start_tx has to be called before fl2k_set_sample_rate
	 * in order to work. */
	if (fl2k_start_tx(fl, tx_callback, &tx, 2) < 0)
		FAIL("Starting FL2K transmission failed\n");

	if (fl2k_set_sample_rate(fl, fs_target) < 0)
		FAIL("Setting FL2K sample rate failed\n");

	uint32_t fs = fl2k_get_sample_rate(fl);
	double fs_c = freq_calib * fs;
	INFO("Exact sample rate: %lu, corrected: %.1f\n", (long unsigned)fs, fs_c);
	tx.fs = fs_c;
	tx_init(&tx);

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
