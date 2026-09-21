#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "jdsp_header.h"

int main(void)
{
	JamesDSPGlobalMemoryAllocation();
	JamesDSPLib *jdsp = (JamesDSPLib *)calloc(1, sizeof(*jdsp));
	assert(jdsp);
	JamesDSPInit(jdsp, 128, 48000.0f);

	char program[] =
		"@init\ninitialRate = srate;\n"
		"@slider\ncoefficient = srate;\n"
		"@sample\nspl0 = coefficient; spl1 = spl1;\n";
	char error[256] = {};
	assert(LiveProgStringParser(jdsp, program, error, sizeof(error)) > 0);
	LiveProgEnable(jdsp);

	JamesDSPSetSampleRate(jdsp, 44100.0f, 1);
	float *rate = NSEEL_VM_getvar(jdsp->eel.vm, "srate");
	float *initialRate = NSEEL_VM_getvar(jdsp->eel.vm, "initialRate");
	assert(rate && initialRate);
	assert(fabsf(jdsp->trueSampleRate - 44100.0f) < 0.1f);
	assert(fabsf(jdsp->fs - 44100.0f) < 0.1f);
	assert(fabsf(*rate - jdsp->fs) < 0.1f);
	assert(fabsf(*initialRate - jdsp->fs) < 0.1f);
	jdsp->tmpBuffer[0][0] = 0.0f;
	jdsp->tmpBuffer[1][0] = 0.0f;
	LiveProgProcess(jdsp, 1);
	assert(fabsf(jdsp->tmpBuffer[0][0] - 44100.0f) < 0.1f);

	char delayProgram[] =
		"@init\ndelaySamples = floor(srate / 1000); position = 0; delayLine[128] = 0;\n"
		"@sample\ninput = spl0; spl0 = delayLine[position]; delayLine[position] = input;\n"
		"position += 1; position >= delaySamples ? position = 0;\n";
	assert(LiveProgStringParser(jdsp, delayProgram, error, sizeof(error)) > 0);
	LiveProgEnable(jdsp);
	float *delaySamples = NSEEL_VM_getvar(jdsp->eel.vm, "delaySamples");
	assert(delaySamples && *delaySamples == 44.0f);
	for (size_t sample = 0; sample < 64; ++sample)
	{
		jdsp->tmpBuffer[0][sample] = sample == 0 ? 1.0f : 0.0f;
		jdsp->tmpBuffer[1][sample] = 0.0f;
	}
	LiveProgProcess(jdsp, 64);
	for (size_t sample = 0; sample < 64; ++sample)
		assert(fabsf(jdsp->tmpBuffer[0][sample] - (sample == 44 ? 1.0f : 0.0f)) < 0.0001f);

	JamesDSPSetSampleRate(jdsp, 96000.0f, 0);
	delaySamples = NSEEL_VM_getvar(jdsp->eel.vm, "delaySamples");
	assert(fabsf(jdsp->trueSampleRate - 96000.0f) < 0.1f);
	assert(fabsf(jdsp->fs - 48000.0f) < 0.1f);
	assert(delaySamples && *delaySamples == 48.0f);
	for (size_t sample = 0; sample < 64; ++sample)
	{
		jdsp->tmpBuffer[0][sample] = sample == 0 ? 1.0f : 0.0f;
		jdsp->tmpBuffer[1][sample] = 0.0f;
	}
	LiveProgProcess(jdsp, 64);
	for (size_t sample = 0; sample < 64; ++sample)
		assert(fabsf(jdsp->tmpBuffer[0][sample] - (sample == 48 ? 1.0f : 0.0f)) < 0.0001f);

	char filterProgram[] =
		"@init\nalpha = exp(-6.283185307179586 * 1000 / srate); state = 0;\n"
		"@sample\nstate = alpha * state + (1 - alpha) * spl0; spl0 = state;\n";
	assert(LiveProgStringParser(jdsp, filterProgram, error, sizeof(error)) > 0);
	LiveProgEnable(jdsp);
	const float deviceRates[] = {44100.0f, 96000.0f};
	const float internalRates[] = {44100.0f, 48000.0f};
	for (size_t rateIndex = 0; rateIndex < 2; ++rateIndex)
	{
		JamesDSPSetSampleRate(jdsp, deviceRates[rateIndex], 0);
		float *alpha = NSEEL_VM_getvar(jdsp->eel.vm, "alpha");
		assert(alpha);
		const float expectedAlpha = expf(-6.283185307179586f * 1000.0f / internalRates[rateIndex]);
		assert(fabsf(*alpha - expectedAlpha) < 0.000001f);
		for (size_t sample = 0; sample < 64; ++sample)
		{
			jdsp->tmpBuffer[0][sample] = sample == 0 ? 1.0f : 0.0f;
			jdsp->tmpBuffer[1][sample] = 0.0f;
		}
		LiveProgProcess(jdsp, 64);
		float expected = 1.0f - expectedAlpha;
		for (size_t sample = 0; sample < 64; ++sample)
		{
			if (sample > 0)
				expected *= expectedAlpha;
			assert(fabsf(jdsp->tmpBuffer[0][sample] - expected) < 0.000001f);
		}
	}

	JamesDSPFree(jdsp);
	free(jdsp);
	puts("bounded native rate refresh passed");
	return 0;
}
