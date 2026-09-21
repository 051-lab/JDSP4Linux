#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>

#include "jdsp_header.h"

extern void JamesDSPProcess(JamesDSPLib *jdsp, size_t n);

static float value(JamesDSPLib *jdsp, const char *name)
{
	float *var = jdsp->eel.vm ? NSEEL_VM_getvar(jdsp->eel.vm, name) : 0;
	assert(var);
	return *var;
}

static void *process_crossfeed(void *data)
{
	JamesDSPLib *jdsp = (JamesDSPLib *)data;
	for (int iteration = 0; iteration < 2000; ++iteration)
	{
		for (size_t sample = 0; sample < 128; ++sample)
		{
			jdsp->tmpBuffer[0][sample] = 0.125f;
			jdsp->tmpBuffer[1][sample] = -0.125f;
		}
		JamesDSPProcess(jdsp, 128);
	}
	return 0;
}

static void *process_during_rate_changes(void *data)
{
	JamesDSPLib *jdsp = (JamesDSPLib *)data;
	for (int iteration = 0; iteration < 256; ++iteration)
		/* Do not write through internal workspace pointers from the test
		 * thread: their lifetime is the race under test. Exercise only the
		 * processing entry while the control thread changes sample rate. */
		JamesDSPProcess(jdsp, 128);
	return 0;
}

int main(void)
{
	JamesDSPLib *jdsp = calloc(1, sizeof(*jdsp));
	JamesDSPGlobalMemoryAllocation();
	JamesDSPInit(jdsp, 128, 48000.0f);
	float gateInputLeft[4] = {0.25f, 0.0f, 0.0f, 0.0f};
	float gateInputRight[4] = {-0.25f, 0.0f, 0.0f, 0.0f};
	float gateOutputLeft[4] = {9.0f, 9.0f, 9.0f, 9.0f};
	float gateOutputRight[4] = {9.0f, 9.0f, 9.0f, 9.0f};
	__atomic_store_n(&jdsp->processingPaused, 1, __ATOMIC_SEQ_CST);
	jdsp->processFloatDeinterleaved(jdsp, gateInputLeft, gateInputRight,
		gateOutputLeft, gateOutputRight, 4);
	for (size_t sample = 0; sample < 4; ++sample)
		assert(gateOutputLeft[sample] == 0.0f && gateOutputRight[sample] == 0.0f);
	assert(__atomic_load_n(&jdsp->processingReaders, __ATOMIC_SEQ_CST) == 0);
	__atomic_store_n(&jdsp->processingPaused, 0, __ATOMIC_SEQ_CST);
	char script[] = "@sample\nspl0 = spl0; spl1 = spl1;\n";
	char error[256] = {};
	assert(LiveProgStringParser(jdsp, script, error, sizeof(error)) > 0);
	const int skipCrossfeedStress = getenv("JDSP_RATE_TSAN_SKIP_CROSSFEED") != NULL;
	pthread_t processor;
	if (!skipCrossfeedStress)
	{
		CrossfeedEnable(jdsp, 1);
		void *longState = jdsp->advXF.convLong_S_S ? (void*)jdsp->advXF.convLong_S_S : (void*)jdsp->advXF.convLong_T_S;
		CrossfeedEnable(jdsp, 1);
		void *sameLongState = jdsp->advXF.convLong_S_S ? (void*)jdsp->advXF.convLong_S_S : (void*)jdsp->advXF.convLong_T_S;
		assert(longState == sameLongState);
		jdsp->advXF.mode = 2;
		jdsp->crossfeedEnabled = 1;
		assert(pthread_create(&processor, 0, process_crossfeed, jdsp) == 0);
		for (int iteration = 0; iteration < 32; ++iteration)
		{
			jdsp->crossfeedForceRefresh = 1;
			CrossfeedEnable(jdsp, 1);
		}
		assert(pthread_join(processor, 0) == 0);
	}

	assert(pthread_create(&processor, 0, process_during_rate_changes, jdsp) == 0);
	const float transitionRates[] = {44100.0f, 48000.0f, 96000.0f, 48000.0f};
	for (int iteration = 0; iteration < 64; ++iteration)
		JamesDSPSetSampleRate(jdsp, transitionRates[iteration % 4], 1);
	assert(pthread_join(processor, 0) == 0);
	assert(fabsf(jdsp->trueSampleRate - 48000.0f) < 0.1f);
	assert(fabsf(jdsp->fs - 48000.0f) < 0.1f);
	/* Ordinary backend notifications use forceRefresh=0. Enabled bass boost
	 * must still be redesigned for the effective processing rate. */
	BassBoostSetParam(jdsp, 6.0f);
	BassBoostEnable(jdsp);
	assert(fabs(jdsp->dbb.fs - 48000.0) < 0.1);
	JamesDSPSetSampleRate(jdsp, 44100.0f, 0);
	assert(fabs(jdsp->dbb.fs - 44100.0) < 0.1);
	for (size_t sample = 0; sample < 32; ++sample)
	{
		jdsp->tmpBuffer[0][sample] = 0.125f;
		jdsp->tmpBuffer[1][sample] = -0.125f;
	}
	JamesDSPProcess(jdsp, 32);
	for (size_t sample = 0; sample < 32; ++sample)
		assert(isfinite(jdsp->tmpBuffer[0][sample]) && isfinite(jdsp->tmpBuffer[1][sample]));
	JamesDSPSetSampleRate(jdsp, 48000.0f, 0);
	assert(fabs(jdsp->dbb.fs - 48000.0) < 0.1);
	JamesDSPSetSampleRate(jdsp, 96000.0f, 0);
	assert(fabsf(jdsp->trueSampleRate - 96000.0f) < 0.1f);
	assert(fabsf(jdsp->fs - 48000.0f) < 0.1f);
	assert(fabs(jdsp->dbb.fs - 48000.0) < 0.1);
	JamesDSPSetSampleRate(jdsp, 48000.0f, 0);
	BassBoostDisable(jdsp);
	/* Invalid input must be rejected before any arithmetic or state change. */
	JamesDSPSetSampleRate(jdsp, 0.0f, 1);
	JamesDSPSetSampleRate(jdsp, -1.0f, 1);
	JamesDSPSetSampleRate(jdsp, NAN, 1);
	JamesDSPSetSampleRate(jdsp, 7999.0f, 1);
	JamesDSPSetSampleRate(jdsp, 192001.0f, 1);
	assert(fabsf(jdsp->trueSampleRate - 48000.0f) < 0.1f);
	assert(fabsf(jdsp->fs - 48000.0f) < 0.1f);

	JamesDSPSetSampleRate(jdsp, 44100.0f, 0);
	assert(fabsf(jdsp->trueSampleRate - 44100.0f) < 0.1f);
	assert(fabsf(jdsp->fs - 44100.0f) < 0.1f);
	assert(fabsf(value(jdsp, "srate") - 44100.0f) < 0.1f);
	JamesDSPSetSampleRate(jdsp, 96000.0f, 1);
	assert(fabsf(jdsp->trueSampleRate - 96000.0f) < 0.1f);
	assert(fabsf(jdsp->fs - 48000.0f) < 0.1f);
	assert(fabsf(value(jdsp, "srate") - 48000.0f) < 0.1f);
	JamesDSPSetSampleRate(jdsp, 0.0f, 1);
	assert(fabsf(jdsp->trueSampleRate - 96000.0f) < 0.1f);

	/* A device-rate change must refresh @init-derived values while retaining
	 * parameters explicitly set by the host, then rerun @slider. */
	char rateDependentProgram[] =
		"@init\ngain = 1; initRate = srate;\n"
		"@slider\ncoefficient = srate * gain;\n"
		"@sample\nspl0 = coefficient; spl1 = spl1;\n";
	assert(LiveProgStringParser(jdsp, rateDependentProgram, error, sizeof(error)) > 0);
	LiveProgEnable(jdsp);
	assert(LiveProgSetVariable(jdsp, "gain", 2.0f));
	assert(fabsf(value(jdsp, "coefficient") - 96000.0f) < 0.1f);
	JamesDSPSetSampleRate(jdsp, 44100.0f, 1);
	assert(fabsf(value(jdsp, "initRate") - 44100.0f) < 0.1f);
	assert(fabsf(value(jdsp, "gain") - 2.0f) < 0.001f);
	assert(fabsf(value(jdsp, "coefficient") - 88200.0f) < 0.1f);
	jdsp->tmpBuffer[0][0] = 0.0f;
	jdsp->tmpBuffer[1][0] = 0.0f;
	LiveProgProcess(jdsp, 1);
	assert(fabsf(jdsp->tmpBuffer[0][0] - 88200.0f) < 0.1f);
	CrossfeedDisable(jdsp);
	JamesDSPSetSampleRate(jdsp, 48000.0f, 1);
	assert(fabsf(value(jdsp, "initRate") - 48000.0f) < 0.1f);
	assert(fabsf(value(jdsp, "gain") - 2.0f) < 0.001f);
	assert(fabsf(value(jdsp, "coefficient") - 96000.0f) < 0.1f);
	jdsp->tmpBuffer[0][0] = 0.0f;
	jdsp->tmpBuffer[1][0] = 0.0f;
	LiveProgProcess(jdsp, 1);
	assert(fabsf(jdsp->tmpBuffer[0][0] - 96000.0f) < 0.1f);
	JamesDSPSetSampleRate(jdsp, 96000.0f, 1);
	assert(fabsf(jdsp->trueSampleRate - 96000.0f) < 0.1f);
	assert(fabsf(jdsp->fs - 48000.0f) < 0.1f);
	assert(fabsf(value(jdsp, "initRate") - 48000.0f) < 0.1f);
	assert(fabsf(value(jdsp, "gain") - 2.0f) < 0.001f);
	assert(fabsf(value(jdsp, "coefficient") - 96000.0f) < 0.1f);

	JamesDSPFree(jdsp);
	free(jdsp);
	puts("rate transition test passed");
	return 0;
}
