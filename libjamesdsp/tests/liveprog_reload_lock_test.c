#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "jdsp_header.h"

typedef struct
{
	JamesDSPLib* dsp;
	char* source;
	int result;
} ReloadArgs;

static void* reloadThread(void* opaque)
{
	ReloadArgs* args = (ReloadArgs*)opaque;
	char error[1024] = {};
	args->result = LiveProgStringParser(args->dsp, args->source, error, sizeof(error));
	return NULL;
}

static long elapsedMilliseconds(const struct timespec* start, const struct timespec* end)
{
	return (end->tv_sec - start->tv_sec) * 1000L +
		(end->tv_nsec - start->tv_nsec) / 1000000L;
}

static int compareLongs(const void* left, const void* right)
{
	const long a = *(const long*)left;
	const long b = *(const long*)right;
	return (a > b) - (a < b);
}

int main(void)
{
	JamesDSPGlobalMemoryAllocation();
	JamesDSPLib dsp = {};
	JamesDSPInit(&dsp, 64, 48000.0f);

	char first[] = "@init\ngain = 2;\n@sample\nspl0 *= gain; spl1 *= gain;\n";
	char second[] = "@init\ngain = 3;\n@sample\nspl0 *= gain; spl1 *= gain;\n";
	char error[1024] = {};
	assert(LiveProgStringParser(&dsp, first, error, sizeof(error)) > 0);
	LiveProgEnable(&dsp);

	JamesDSPSetLiveProgLoadDelayForTests(250);
	ReloadArgs args = {&dsp, second, 0};
	pthread_t thread;
	assert(pthread_create(&thread, NULL, reloadThread, &args) == 0);
	while(!JamesDSPLiveProgLoadStartedForTests())
		usleep(1000);

	long latencyMs[64];
	for(size_t measurement = 0; measurement < 64; ++measurement)
	{
		for(size_t i = 0; i < 64; ++i)
		{
			dsp.tmpBuffer[0][i] = 1.0f;
			dsp.tmpBuffer[1][i] = 1.0f;
		}
		struct timespec start, end;
		clock_gettime(CLOCK_MONOTONIC, &start);
		jdsp_lock(&dsp);
		LiveProgProcess(&dsp, 64);
		jdsp_unlock(&dsp);
		clock_gettime(CLOCK_MONOTONIC, &end);
		latencyMs[measurement] = elapsedMilliseconds(&start, &end);
		assert(latencyMs[measurement] < 100);
	}
	qsort(latencyMs, 64, sizeof(latencyMs[0]), compareLongs);
	printf("callback latency max=%ldms p99=%ldms during slow reload\n",
		latencyMs[63], latencyMs[63]);
	assert(dsp.tmpBuffer[0][0] == 2.0f);
	assert(dsp.tmpBuffer[1][0] == 2.0f);

	assert(pthread_join(thread, NULL) == 0);
	JamesDSPSetLiveProgLoadDelayForTests(0);
	assert(args.result > 0);
	for(size_t i = 0; i < 64; ++i)
	{
		dsp.tmpBuffer[0][i] = 1.0f;
		dsp.tmpBuffer[1][i] = 1.0f;
	}
	jdsp_lock(&dsp);
	LiveProgProcess(&dsp, 64);
	jdsp_unlock(&dsp);
	assert(dsp.tmpBuffer[0][0] == 3.0f);
	assert(dsp.tmpBuffer[1][0] == 3.0f);

	char parallel[] = "@init\ngain = 5;\n@sample\nspl0 *= gain; spl1 *= gain;\n";
	JamesDSPSetLiveProgLoadDelayForTests(200);
	ReloadArgs firstParallel = {&dsp, parallel, 0};
	ReloadArgs secondParallel = {&dsp, parallel, 0};
	pthread_t firstThread, secondThread;
	assert(pthread_create(&firstThread, NULL, reloadThread, &firstParallel) == 0);
	while(!JamesDSPLiveProgLoadStartedForTests())
		usleep(1000);
	assert(pthread_create(&secondThread, NULL, reloadThread, &secondParallel) == 0);
	assert(pthread_join(firstThread, NULL) == 0);
	assert(pthread_join(secondThread, NULL) == 0);
	assert(firstParallel.result > 0 && secondParallel.result > 0);
	assert(JamesDSPLiveProgMaxConcurrentLoadsForTests() == 1);
	JamesDSPSetLiveProgLoadDelayForTests(0);

	char stale[] = "@init\ngain = 4;\n@sample\nspl0 *= gain; spl1 *= gain;\n";
	JamesDSPSetLiveProgLoadDelayForTests(250);
	ReloadArgs staleArgs = {&dsp, stale, 0};
	assert(pthread_create(&thread, NULL, reloadThread, &staleArgs) == 0);
	while(!JamesDSPLiveProgLoadStartedForTests())
		usleep(1000);
	JamesDSPSetSampleRate(&dsp, 44100.0f, 0);
	assert(pthread_join(thread, NULL) == 0);
	JamesDSPSetLiveProgLoadDelayForTests(0);
	assert(staleArgs.result < 0);
	for(size_t i = 0; i < 64; ++i)
	{
		dsp.tmpBuffer[0][i] = 1.0f;
		dsp.tmpBuffer[1][i] = 1.0f;
	}
	jdsp_lock(&dsp);
	LiveProgProcess(&dsp, 64);
	jdsp_unlock(&dsp);
	assert(dsp.tmpBuffer[0][0] == 5.0f);
	assert(dsp.tmpBuffer[1][0] == 5.0f);

	JamesDSPFree(&dsp);
	JamesDSPGlobalMemoryDeallocation();
	return 0;
}
