#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "jdsp_header.h"

extern void JamesDSPSetBufferAllocationFailureForTests(int fail);

static void process_block(JamesDSPLib *jdsp, size_t frames)
{
	float *input_l = calloc(frames, sizeof(*input_l));
	float *input_r = calloc(frames, sizeof(*input_r));
	float *output_l = calloc(frames, sizeof(*output_l));
	float *output_r = calloc(frames, sizeof(*output_r));
	assert(input_l && input_r && output_l && output_r);

	input_l[0] = 1.0f;
	input_r[0] = -1.0f;
	jdsp->processFloatDeinterleaved(jdsp, input_l, input_r, output_l, output_r, frames);
	for (size_t i = 0; i < frames; ++i)
		assert(isfinite(output_l[i]) && isfinite(output_r[i]));

	free(input_l);
	free(input_r);
	free(output_l);
	free(output_r);
}

int main(void)
{
	JamesDSPLib *jdsp = calloc(1, sizeof(*jdsp));
	assert(jdsp);
	JamesDSPGlobalMemoryAllocation();
	JamesDSPInit(jdsp, 128, 48000.0f);
	float *initial_buffer = jdsp->tmpBuffer[0];
	JamesDSPSetSampleRate(jdsp, 0.0f, 0);
	assert(jdsp->trueSampleRate == 48000.0f);
	assert(jdsp->tmpBuffer[0] == initial_buffer);
	JamesDSPSetSampleRate(jdsp, NAN, 0);
	assert(jdsp->trueSampleRate == 48000.0f);
	assert(jdsp->tmpBuffer[0] == initial_buffer);
	JamesDSPSetSampleRate(jdsp, INFINITY, 0);
	assert(jdsp->trueSampleRate == 48000.0f);
	assert(jdsp->tmpBuffer[0] == initial_buffer);
	JamesDSPSetSampleRate(jdsp, 7999.0f, 0);
	assert(jdsp->trueSampleRate == 48000.0f);
	assert(jdsp->tmpBuffer[0] == initial_buffer);
	JamesDSPSetSampleRate(jdsp, 192001.0f, 0);
	assert(jdsp->trueSampleRate == 48000.0f);
	assert(jdsp->tmpBuffer[0] == initial_buffer);
	JamesDSPSetBufferAllocationFailureForTests(1);
	JamesDSPSetSampleRate(jdsp, 8000.0f, 0);
	assert(jdsp->trueSampleRate == 48000.0f);
	assert(jdsp->fs == 48000.0f);
	assert(!jdsp->enableASRC);
	assert(jdsp->tmpBuffer[0] == initial_buffer);
	process_block(jdsp, 128);
	JamesDSPSetSampleRate(jdsp, 8000.0f, 0);
	float *asrc_buffer = jdsp->tmpBuffer[0];
	float *asrc_decimator = jdsp->asrc[0].polyphaseDecimator.pfb;
	float *asrc_interpolator = jdsp->asrc[0].polyphaseInterpolator.pfb;
	JamesDSPSetBufferAllocationFailureForTests(1);
	JamesDSPSetSampleRate(jdsp, 16000.0f, 0);
	assert(jdsp->trueSampleRate == 8000.0f);
	assert(jdsp->fs == 48000.0f);
	assert(jdsp->enableASRC);
	assert(jdsp->tmpBuffer[0] == asrc_buffer);
	assert(jdsp->asrc[0].polyphaseDecimator.pfb == asrc_decimator);
	assert(jdsp->asrc[0].polyphaseInterpolator.pfb == asrc_interpolator);
	process_block(jdsp, 128);
	JamesDSPLib *inactive = calloc(1, sizeof(*inactive));
	assert(inactive);
	JamesDSPSetBufferAllocationFailureForTests(1);
	JamesDSPInit(inactive, 128, 48000.0f);
	assert(inactive->tmpBuffer[0] == NULL);
	assert(inactive->tmpBuffer[1] == NULL);
	JamesDSPFree(inactive);
	free(inactive);
	JamesDSPReallocateBlock(jdsp, 0);
	assert(jdsp->blockSizeMax == 128);
	assert(jdsp->tmpBuffer[0] == asrc_buffer);
	JamesDSPReallocateBlock(jdsp, SIZE_MAX);
	assert(jdsp->blockSizeMax == 128);
	assert(jdsp->tmpBuffer[0] == asrc_buffer);

	const float rates[] = { 8000.0f, 16000.0f, 22050.0f, 32000.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f };
	const size_t frames[] = { 1, 63, 128, 257, 1024, 4096 };
	for (size_t rate_index = 0; rate_index < sizeof(rates) / sizeof(rates[0]); ++rate_index)
	{
		JamesDSPSetSampleRate(jdsp, rates[rate_index], 0);
		for (size_t frame_index = 0; frame_index < sizeof(frames) / sizeof(frames[0]); ++frame_index)
		{
			JamesDSPReallocateBlock(jdsp, frames[frame_index]);
			process_block(jdsp, frames[frame_index]);
		}
	}

	JamesDSPFree(jdsp);
	free(jdsp);
	puts("ASRC capacity test passed");
	return 0;
}
