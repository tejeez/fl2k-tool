#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <osmo-fl2k.h>

#define FAIL(...) { fprintf(stderr, __VA_ARGS__); goto end; }
#define INFO(...) { fprintf(stderr, __VA_ARGS__); }

struct transmitter {
	uint32_t fs; // Sample rate
	int8_t *buf; // Buffer, allocated at init
	uint64_t phase, freq; // Oscillator phase and frequency
	char initialized;
};

void tx_init(struct transmitter *tx)
{
	// tx->fs is set by main
	tx->buf = malloc(FL2K_BUF_LEN * sizeof(&tx->buf));
	tx->phase = 0;
	tx->freq = 1e6 / (double)tx->fs * ((double)(1ULL<<63) * 2.0);
	//INFO("TX freq: %llu\n", (long long unsigned)tx->freq);
	tx->initialized = 1;
}

void tx_callback(fl2k_data_info_t *fldata)
{
	struct transmitter *tx = fldata->ctx;
	if (!tx->initialized)
		return;

	int8_t *b = tx->buf;
	long unsigned i, bl = fldata->len;
	//INFO("Buffer length: %lu", bl);

	for (i = 0; i < bl; i++) {
		tx->phase += tx->freq;
		*b++ = tx->phase >> (64-8);
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

	tx.fs = fl2k_get_sample_rate(fl);
	INFO("Exact sample rate: %lu\n", (long unsigned)tx.fs);
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
