#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "jdsp_header.h"

typedef struct {
	JamesDSPLib *dsp;
	char *source;
	int result;
} ReloadArgs;

static void *reload_thread(void *opaque)
{
	ReloadArgs *args = (ReloadArgs *)opaque;
	char error[256] = {};
	args->result = LiveProgStringParser(args->dsp, args->source, error, sizeof(error));
	return NULL;
}

static void *process_thread(void *opaque)
{
	JamesDSPLib *dsp = (JamesDSPLib *)opaque;
	for (int i = 0; i < 4; ++i)
		JamesDSPProcess(dsp, 64);
	return NULL;
}

static long elapsed_ms(const struct timespec *start, const struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) * 1000L +
		(end->tv_nsec - start->tv_nsec) / 1000000L;
}

int main(void)
{
	JamesDSPGlobalMemoryAllocation();
	JamesDSPLib dsp = {};
	JamesDSPInit(&dsp, 64, 48000.0f);
	char slow[] = "@init\ngain = 2;\n@sample\nloop(500000, counter += 1); spl0 *= gain; spl1 *= gain;\n";
	char replacement[] = "@init\ngain = 3;\n@sample\nspl0 *= gain; spl1 *= gain;\n";
	char error[256] = {};
	assert(LiveProgStringParser(&dsp, slow, error, sizeof(error)) > 0);
	LiveProgEnable(&dsp);
	for (size_t i = 0; i < 64; ++i) {
		dsp.tmpBuffer[0][i] = 1.0f;
		dsp.tmpBuffer[1][i] = 1.0f;
	}

	pthread_t processor;
	assert(pthread_create(&processor, NULL, process_thread, &dsp) == 0);
	usleep(1000);
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);
	ReloadArgs reload = {&dsp, replacement, 0};
	pthread_t reloader;
	assert(pthread_create(&reloader, NULL, reload_thread, &reload) == 0);
	assert(pthread_join(reloader, NULL) == 0);
	clock_gettime(CLOCK_MONOTONIC, &end);
	assert(pthread_join(processor, NULL) == 0);
	long reload_ms = elapsed_ms(&start, &end);
	printf("reload latency during callback=%ldms\n", reload_ms);
	assert(reload.result > 0);
	/* The callback must not make control-side publication wait on slow EEL. */
	const char *limitText = getenv("JDSP_PUBLICATION_LATENCY_LIMIT_MS");
	const long limit = limitText ? strtol(limitText, NULL, 10) : 100;
	assert(limit > 0 && reload_ms < limit);

	JamesDSPSetSampleRate(&dsp, 44100.0f, 0);
	JamesDSPFree(&dsp);
	JamesDSPGlobalMemoryDeallocation();
	puts("liveprog publication test passed");
	return 0;
}
