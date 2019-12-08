#include <stdio.h>
#include <osmo-fl2k.h>

#define FAIL(...) { fprintf(stderr, __VA_ARGS__); goto end; }
#define INFO(...) { fprintf(stderr, __VA_ARGS__); }
int main()
{
	uint32_t fs, fs_target = 100e6;
	fl2k_dev_t *fl = NULL;
	if(fl2k_open(&fl, 0) < 0)
		FAIL("Opening FL2K failed\n");
	if(fl2k_set_sample_rate(fl, fs_target) < 0)
		FAIL("Setting FL2K sample rate failed\n");
	fs = fl2k_get_sample_rate(fl);
	INFO("Exact sample rate: %lu\n", (long unsigned)fs);
end:
	if (fl != NULL)
		fl2k_close(fl);
	return 0;
}
