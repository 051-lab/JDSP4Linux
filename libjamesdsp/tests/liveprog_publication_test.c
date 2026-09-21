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

typedef struct {
	JamesDSPLib *dsp;
	int result;
} ControlArgs;

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

static void *control_thread(void *opaque)
{
	ControlArgs *args = (ControlArgs *)opaque;
	args->result = 1;
	const char *controlIterationsText = getenv("JDSP_PUBLICATION_CONTROL_ITERATIONS");
	const int controlIterations = controlIterationsText ? atoi(controlIterationsText) : 64;
	for (int i = 0; i < controlIterations; ++i)
	{
		if (!LiveProgSetVariable(args->dsp, "slider", 1.0f + (float)(i % 4)))
			args->result = 0;
		JamesDSPSetSampleRate(args->dsp, (i & 1) ? 44100.0f : 48000.0f, 0);
	}
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
	char slow[256];
	const char *slowIterations = getenv("JDSP_PUBLICATION_SLOW_ITERATIONS");
	snprintf(slow, sizeof(slow), "@init\ngain = 2;\n@sample\nloop(%s, counter += 1); spl0 *= gain; spl1 *= gain;\n",
		slowIterations ? slowIterations : "500000");
	char replacement[] = "@init\ngain = 3; slider = 3;\n@slider\ngain = slider;\n@sample\nspl0 *= gain; spl1 *= gain;\n";
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

	ControlArgs control = {&dsp, 0};
	assert(pthread_create(&processor, NULL, process_thread, &dsp) == 0);
	assert(pthread_create(&reloader, NULL, control_thread, &control) == 0);
	assert(pthread_join(reloader, NULL) == 0);
	assert(pthread_join(processor, NULL) == 0);
	assert(control.result == 1);

	/* The callback must not make control-side publication wait on slow EEL. */
	const char *limitText = getenv("JDSP_PUBLICATION_LATENCY_LIMIT_MS");
	const long limit = limitText ? strtol(limitText, NULL, 10) : 100;
	assert(limit > 0 && reload_ms < limit);

	JamesDSPSetSampleRate(&dsp, 44100.0f, 0);
	LiveProgDestructor(&dsp);
	assert(LiveProgSetVariable(&dsp, "slider", 2.0f) == 0);
	assert(pthread_mutex_trylock(&dsp.m_in_processing) == 0);
	pthread_mutex_unlock(&dsp.m_in_processing);
	LiveProgConstructor(&dsp);
	JamesDSPFree(&dsp);
	JamesDSPGlobalMemoryDeallocation();
	puts("liveprog publication test passed");
	return 0;
}
