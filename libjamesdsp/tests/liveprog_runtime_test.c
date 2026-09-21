#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jdsp_header.h"

static float *variable(JamesDSPLib *jdsp, const char *name)
{
	return jdsp->eel.vm ? NSEEL_VM_getvar(jdsp->eel.vm, name) : 0;
}

static int load_file(JamesDSPLib *jdsp, const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file)
	{
		perror(path);
		return 0;
	}

	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return 0;
	}
	long length = ftell(file);
	if (length < 0 || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return 0;
	}

	char *source = (char*)malloc((size_t)length + 1);
	if (!source || fread(source, 1, (size_t)length, file) != (size_t)length)
	{
		free(source);
		fclose(file);
		return 0;
	}
	source[length] = '\0';
	fclose(file);

	char error[1024] = {};
	int result = LiveProgStringParser(jdsp, source, error, sizeof(error));
	if (result <= 0)
		fprintf(stderr, "%s: parser returned %d: %s\n", path, result, error);
	free(source);
	return result > 0;
}

static void assert_gain_block(JamesDSPLib *jdsp, const float *left, const float *right, size_t length, float gain)
{
	for (size_t i = 0; i < length; ++i)
	{
		jdsp->tmpBuffer[0][i] = left[i];
		jdsp->tmpBuffer[1][i] = right[i];
	}
	LiveProgProcess(jdsp, length);
	for (size_t i = 0; i < length; ++i)
	{
		assert(fabsf(jdsp->tmpBuffer[0][i] - left[i] * gain) < 0.0001f);
		assert(fabsf(jdsp->tmpBuffer[1][i] - right[i] * gain) < 0.0001f);
	}
}

static void exercise_stimuli(JamesDSPLib *jdsp)
{
	uint32_t seed = 0x2468ACE1u;
	for (int stimulus = 0; stimulus < 5; ++stimulus)
	{
		for (size_t sample = 0; sample < 32; ++sample)
		{
			float left = 0.0f;
			float right = 0.0f;
			switch (stimulus)
			{
			case 1:
				left = sample == 0 ? 1.0f : 0.0f;
				break;
			case 2:
				right = sample == 0 ? -1.0f : 0.0f;
				break;
			case 3:
				left = 0.25f * sinf((float)sample * 0.37f);
				right = -0.125f * cosf((float)sample * 0.19f);
				break;
			case 4:
				left = sample < 16 ? 0.0f : 0.25f;
				right = sample < 8 ? -0.125f : 0.125f;
				break;
			default:
				seed = seed * 1664525u + 1013904223u;
				left = (float)(seed >> 8) / 16777216.0f * 0.5f - 0.25f;
				seed = seed * 1664525u + 1013904223u;
				right = (float)(seed >> 8) / 16777216.0f * 0.5f - 0.25f;
				break;
			}
			jdsp->tmpBuffer[0][sample] = left;
			jdsp->tmpBuffer[1][sample] = right;
		}
		LiveProgProcess(jdsp, 32);
		for (size_t sample = 0; sample < 32; ++sample)
			assert(isfinite(jdsp->tmpBuffer[0][sample]) && isfinite(jdsp->tmpBuffer[1][sample]));
	}
}

static void exercise_control_extrema(JamesDSPLib *jdsp, int argc, char **argv)
{
	/* Arguments are name/default/min/max quadruples supplied by the inventory
	 * runner. Legacy variables may not be live-safe; the native setter reports
	 * that capability and those controls are still checked for finite state. */
	for (int argument = 3; argument + 3 < argc; argument += 4)
	{
		const char *name = argv[argument];
		const float values[] = {
			strtof(argv[argument + 1], NULL),
			strtof(argv[argument + 2], NULL),
			strtof(argv[argument + 3], NULL),
		};
		for (size_t valueIndex = 0; valueIndex < sizeof(values) / sizeof(values[0]); ++valueIndex)
		{
			LiveProgSetVariable(jdsp, name, values[valueIndex]);
			for (size_t sample = 0; sample < 32; ++sample)
			{
				jdsp->tmpBuffer[0][sample] = 0.125f * sinf((float)sample * 0.17f);
				jdsp->tmpBuffer[1][sample] = -0.125f * cosf((float)sample * 0.11f);
			}
			LiveProgProcess(jdsp, 32);
			for (size_t sample = 0; sample < 32; ++sample)
			{
				assert(isfinite(jdsp->tmpBuffer[0][sample]));
				assert(isfinite(jdsp->tmpBuffer[1][sample]));
				assert(fabsf(jdsp->tmpBuffer[0][sample]) < 1000000.0f);
				assert(fabsf(jdsp->tmpBuffer[1][sample]) < 1000000.0f);
			}
		}
	}
}

static void exercise_block_sizes(JamesDSPLib *jdsp)
{
	const size_t lengths[] = {1, 2, 7, 32, 77};
	for (size_t lengthIndex = 0; lengthIndex < sizeof(lengths) / sizeof(lengths[0]); ++lengthIndex)
	{
		const size_t length = lengths[lengthIndex];
		for (size_t sample = 0; sample < length; ++sample)
		{
			jdsp->tmpBuffer[0][sample] = 0.2f * sinf((float)sample * 0.13f);
			jdsp->tmpBuffer[1][sample] = -0.15f * cosf((float)sample * 0.23f);
		}
		LiveProgProcess(jdsp, length);
		for (size_t sample = 0; sample < length; ++sample)
		{
			assert(isfinite(jdsp->tmpBuffer[0][sample]));
			assert(isfinite(jdsp->tmpBuffer[1][sample]));
			assert(fabsf(jdsp->tmpBuffer[0][sample]) < 1000000.0f);
			assert(fabsf(jdsp->tmpBuffer[1][sample]) < 1000000.0f);
		}
	}
}

int main(int argc, char **argv)
{
	JamesDSPLib *jdsp = (JamesDSPLib*)malloc(sizeof(JamesDSPLib));
	JamesDSPGlobalMemoryAllocation();
	JamesDSPInit(jdsp, 77, 48000.0f);

	char valid[] =
		"@init\n"
		"slider1 = 1; gain = 1; blocks = 0; samples = 0;\n"
		"@slider\n"
		"gain = slider1;\n"
		"@block\n"
		"blocks += 1; seen = samplesblock;\n"
		"@sample\n"
		"samples += 1; spl0 *= gain; spl1 *= gain;\n";
	char error[256] = {};
	assert(LiveProgStringParser(jdsp, valid, error, sizeof(error)) > 0);
	LiveProgEnable(jdsp);
	if (!jdsp->liveprogEnabled)
		fprintf(stderr, "baseline enable failed: vmFs=%p compile=%d fs=%g vmRate=%g\n",
			(void*)jdsp->eel.vmFs, jdsp->eel.compileSucessfully, jdsp->fs,
			jdsp->eel.vmFs ? *jdsp->eel.vmFs : -1.0f);
	assert(jdsp->liveprogEnabled);
	assert(variable(jdsp, "blocks") && *variable(jdsp, "blocks") == 0);
	LiveProgDisable(jdsp);
	assert(!jdsp->liveprogEnabled);
	jdsp->tmpBuffer[0][0] = 0.25f;
	jdsp->tmpBuffer[1][0] = -0.5f;
	JamesDSPProcess(jdsp, 1);
	assert(fabsf(jdsp->tmpBuffer[0][0] - 0.25f) < 0.0001f);
	assert(fabsf(jdsp->tmpBuffer[1][0] + 0.5f) < 0.0001f);
	LiveProgEnable(jdsp);

	for (size_t i = 0; i < 4; ++i)
	{
		jdsp->tmpBuffer[0][i] = 1.0f;
		jdsp->tmpBuffer[1][i] = 2.0f;
	}
	LiveProgProcess(jdsp, 4);
	assert(fabsf(jdsp->tmpBuffer[0][0] - 1.0f) < 0.0001f);
	assert(fabsf(jdsp->tmpBuffer[1][0] - 2.0f) < 0.0001f);
	assert(variable(jdsp, "blocks") && *variable(jdsp, "blocks") == 1);
	assert(variable(jdsp, "seen") && *variable(jdsp, "seen") == 4);
	assert(variable(jdsp, "samples") && *variable(jdsp, "samples") == 4);
	assert(LiveProgSetVariable(jdsp, "slider1", 2.0f));
	assert(variable(jdsp, "gain") && *variable(jdsp, "gain") == 2.0f);
	{
		float left[77] = {0};
		float right[77] = {0};
		assert_gain_block(jdsp, left, right, 32, 2.0f); /* silence */

		memset(left, 0, sizeof(left));
		memset(right, 0, sizeof(right));
		left[0] = 1.0f;
		right[31] = -1.0f;
		assert_gain_block(jdsp, left, right, 32, 2.0f); /* opposing impulses */

		for (size_t i = 0; i < 32; ++i)
		{
			left[i] = sinf((float)i * 0.37f);
			right[i] = cosf((float)i * 0.19f);
		}
		assert_gain_block(jdsp, left, right, 32, 2.0f); /* deterministic sine/cosine */

		for (size_t i = 0; i < 32; ++i)
		{
			left[i] = i < 16 ? 0.0f : 1.0f;
			right[i] = i < 8 ? -0.5f : 0.5f;
		}
		assert_gain_block(jdsp, left, right, 32, 2.0f); /* step */

		uint32_t seed = 0x13579BDFu;
		for (size_t i = 0; i < 32; ++i)
		{
			seed = seed * 1664525u + 1013904223u;
			left[i] = (float)(seed >> 8) / 16777216.0f * 2.0f - 1.0f;
			seed = seed * 1664525u + 1013904223u;
			right[i] = (float)(seed >> 8) / 16777216.0f * 2.0f - 1.0f;
		}
		assert_gain_block(jdsp, left, right, 32, 2.0f); /* seeded noise */

		const size_t lengths[] = {1, 2, 7, 32, 77};
		const float controls[] = {0.25f, 0.5f, 1.5f};
		for (size_t control = 0; control < sizeof(controls) / sizeof(controls[0]); ++control)
		{
			assert(LiveProgSetVariable(jdsp, "slider1", controls[control]));
			for (size_t lengthIndex = 0; lengthIndex < sizeof(lengths) / sizeof(lengths[0]); ++lengthIndex)
			{
				for (size_t i = 0; i < lengths[lengthIndex]; ++i)
				{
					left[i] = (float)(i + 1) / 8.0f;
					right[i] = -(float)(i + 1) / 16.0f;
				}
				assert_gain_block(jdsp, left, right, lengths[lengthIndex], controls[control]);
			}
		}
		assert(LiveProgSetVariable(jdsp, "slider1", 1.0f));

		const float rates[] = {44100.0f, 48000.0f, 96000.0f, 48000.0f};
		for (size_t rateIndex = 0; rateIndex < sizeof(rates) / sizeof(rates[0]); ++rateIndex)
		{
			JamesDSPSetSampleRate(jdsp, rates[rateIndex], 0);
			assert(fabsf(jdsp->trueSampleRate - rates[rateIndex]) < 0.1f);
			const float expectedVmRate = rates[rateIndex] > 48000.0f ? 48000.0f : rates[rateIndex];
			assert(variable(jdsp, "srate") && fabsf(*variable(jdsp, "srate") - expectedVmRate) < 0.1f);
			for (size_t i = 0; i < 32; ++i)
			{
				left[i] = (float)i / 32.0f;
				right[i] = -(float)i / 32.0f;
			}
			for (size_t i = 0; i < 32; ++i)
			{
				jdsp->tmpBuffer[0][i] = left[i];
				jdsp->tmpBuffer[1][i] = right[i];
			}
			LiveProgProcess(jdsp, 32);
			for (size_t i = 0; i < 32; ++i)
				assert(isfinite(jdsp->tmpBuffer[0][i]) && isfinite(jdsp->tmpBuffer[1][i]));
		}
		JamesDSPSetSampleRate(jdsp, 48000.0f, 0);
	}
	assert(LiveProgSetVariable(jdsp, "slider1", 1.0f));

	char invalid[] = "@init\ngain = 99;\n@sample\nspl0 = ;\n";
	memset(error, 0, sizeof(error));
	assert(LiveProgStringParser(jdsp, invalid, error, sizeof(error)) < 0);
	assert(jdsp->liveprogEnabled);
	assert(variable(jdsp, "gain") && fabsf(*variable(jdsp, "gain") - 1.0f) < 0.0001f);
	jdsp->tmpBuffer[0][0] = 1.0f;
	jdsp->tmpBuffer[1][0] = 2.0f;
	LiveProgProcess(jdsp, 1);
	assert(fabsf(jdsp->tmpBuffer[0][0] - 1.0f) < 0.0001f);
	/* Rate refresh reruns @init; the final transition cycle and the process
	 * after the rejected reload each execute @block once. */
	assert(variable(jdsp, "blocks") && *variable(jdsp, "blocks") == 2);
	char recovered[] =
		"@init\n"
		"gain = 3;\n"
		"@sample\n"
		"spl0 *= gain; spl1 *= gain;\n";
	assert(LiveProgStringParser(jdsp, recovered, error, sizeof(error)) > 0);
	assert(jdsp->liveprogEnabled);
	LiveProgEnable(jdsp);
	jdsp->tmpBuffer[0][0] = 1.0f;
	jdsp->tmpBuffer[1][0] = 1.0f;
	LiveProgProcess(jdsp, 1);
	assert(fabsf(jdsp->tmpBuffer[0][0] - 3.0f) < 0.0001f);

	char duplicate[] = "@init\n@init\n@sample\n";
	assert(LiveProgStringParser(jdsp, duplicate, error, sizeof(error)) == -6);
	char unsupported[] = "@init\n@sample\n@gfx\n";
	assert(LiveProgStringParser(jdsp, unsupported, error, sizeof(error)) == -8);

	char legacy[] =
		"@init\n"
		"dB = -8; DB_2_LOG = 0.11512925464970228420089957273422; gainLin = exp(dB * DB_2_LOG);\n"
		"@sample\n"
		"spl0 *= gainLin; spl1 *= gainLin;\n";
	assert(LiveProgStringParser(jdsp, legacy, error, sizeof(error)) > 0);
	LiveProgEnable(jdsp);
	jdsp->tmpBuffer[0][0] = 1.0f;
	jdsp->tmpBuffer[1][0] = 1.0f;
	LiveProgProcess(jdsp, 1);
	assert(fabsf(jdsp->tmpBuffer[0][0] - 0.398107f) < 0.0001f);
	assert(!LiveProgSetVariable(jdsp, "dB", 0.0f));
	assert(variable(jdsp, "dB") && fabsf(*variable(jdsp, "dB")) < 0.0001f);
	jdsp->tmpBuffer[0][0] = 1.0f;
	jdsp->tmpBuffer[1][0] = 1.0f;
	LiveProgProcess(jdsp, 1);
	assert(fabsf(jdsp->tmpBuffer[0][0] - 0.398107f) < 0.0001f);
	char legacyUpdated[] =
		"@init\n"
		"dB = 0; DB_2_LOG = 0.11512925464970228420089957273422; gainLin = exp(dB * DB_2_LOG);\n"
		"@sample\n"
		"spl0 *= gainLin; spl1 *= gainLin;\n";
	assert(LiveProgStringParser(jdsp, legacyUpdated, error, sizeof(error)) > 0);
	LiveProgEnable(jdsp);
	jdsp->tmpBuffer[0][0] = 1.0f;
	jdsp->tmpBuffer[1][0] = 1.0f;
	LiveProgProcess(jdsp, 1);
	assert(fabsf(jdsp->tmpBuffer[0][0] - 1.0f) < 0.0001f);
	char nonFinite[] = "@sample\nspl0 = 0 / 0; spl1 = 1 / 0;\n";
	assert(LiveProgStringParser(jdsp, nonFinite, error, sizeof(error)) > 0);
	LiveProgEnable(jdsp);
	jdsp->tmpBuffer[0][0] = 1.0f;
	jdsp->tmpBuffer[1][0] = -1.0f;
	LiveProgProcess(jdsp, 1);
	assert(jdsp->liveprogNonFiniteSamples == 2);
	assert(jdsp->tmpBuffer[0][0] == 0.0f && jdsp->tmpBuffer[1][0] == 0.0f);

	for (int i = 1; i < argc; ++i)
	{
		if (argv[i][0] == '-')
			break;
		const uint64_t nonFiniteBefore = jdsp->liveprogNonFiniteSamples;
		assert(load_file(jdsp, argv[i]));
		LiveProgEnable(jdsp);
		exercise_stimuli(jdsp);
		if (i == 1 && argc > 2 && strcmp(argv[2], "--controls") == 0)
			exercise_control_extrema(jdsp, argc, argv);
		exercise_block_sizes(jdsp);
		if (argv[i][0] != '-')
		{
			const float genericRates[] = {44100.0f, 48000.0f};
			for (size_t rateIndex = 0; rateIndex < sizeof(genericRates) / sizeof(genericRates[0]); ++rateIndex)
			{
				JamesDSPSetSampleRate(jdsp, genericRates[rateIndex], 0);
				assert(load_file(jdsp, argv[i]));
				LiveProgEnable(jdsp);
				exercise_block_sizes(jdsp);
			}
			JamesDSPSetSampleRate(jdsp, 48000.0f, 0);
			assert(load_file(jdsp, argv[i]));
			LiveProgEnable(jdsp);
		}
		if (strstr(argv[i], "stftDenoise.eel"))
		{
			const float stftRates[] = {48000.0f, 44100.0f};
			for (size_t rateIndex = 0; rateIndex < sizeof(stftRates) / sizeof(stftRates[0]); ++rateIndex)
			{
				JamesDSPSetSampleRate(jdsp, stftRates[rateIndex], 0);
				assert(load_file(jdsp, argv[i]));
				LiveProgEnable(jdsp);
				assert(variable(jdsp, "srate") && fabsf(*variable(jdsp, "srate") - stftRates[rateIndex]) < 0.01f);
				/* frameLen=2048, overlap=4, and tau=0.9 in nesInit. */
				const float expectedSmoothing = expf(-(2048.0f / 2.0f / stftRates[rateIndex]) / 0.9f);
				assert(variable(jdsp, "nes1.a") && fabsf(*variable(jdsp, "nes1.a") - expectedSmoothing) < 0.0001f);
				assert(*variable(jdsp, "nes1.a") > 0.0f && *variable(jdsp, "nes1.a") < 1.0f);

				/* Exercise the frame accumulator with deterministic step and sine
				 * stimuli; output must remain finite and bounded by the input scale. */
				for (size_t block = 0; block < 40; ++block)
				{
					for (size_t sample = 0; sample < 77; ++sample)
					{
						const size_t absoluteSample = block * 77 + sample;
						const float sine = sinf((float)absoluteSample * 0.071f);
						const float step = absoluteSample < 900 ? 0.0f : 0.25f;
						jdsp->tmpBuffer[0][sample] = sine * 0.25f + step;
						jdsp->tmpBuffer[1][sample] = -sine * 0.125f - step;
					}
					LiveProgProcess(jdsp, 77);
					for (size_t sample = 0; sample < 77; ++sample)
					{
						assert(isfinite(jdsp->tmpBuffer[0][sample]) && isfinite(jdsp->tmpBuffer[1][sample]));
						assert(fabsf(jdsp->tmpBuffer[0][sample]) <= 1.0f);
						assert(fabsf(jdsp->tmpBuffer[1][sample]) <= 1.0f);
					}
				}
			}
			JamesDSPSetSampleRate(jdsp, 48000.0f, 0);
			assert(load_file(jdsp, argv[i]));
			LiveProgEnable(jdsp);
		}
		if (strstr(argv[i], "highpass200Hz.eel"))
		{
			const float highpassRates[] = {44100.0f, 48000.0f};
			for (size_t rateIndex = 0; rateIndex < sizeof(highpassRates) / sizeof(highpassRates[0]); ++rateIndex)
			{
				float leftResponse[8] = {0};
				JamesDSPSetSampleRate(jdsp, highpassRates[rateIndex], 0);
				assert(load_file(jdsp, argv[i]));
				LiveProgEnable(jdsp);
				for (size_t sample = 0; sample < 8; ++sample)
				{
					jdsp->tmpBuffer[0][sample] = sample == 0 ? 1.0f : 0.0f;
					jdsp->tmpBuffer[1][sample] = 0.0f;
				}
				LiveProgProcess(jdsp, 8);
				for (size_t sample = 0; sample < 8; ++sample)
				{
					leftResponse[sample] = jdsp->tmpBuffer[0][sample];
					assert(jdsp->tmpBuffer[1][sample] == 0.0f);
				}

				assert(load_file(jdsp, argv[i]));
				LiveProgEnable(jdsp);
				for (size_t sample = 0; sample < 8; ++sample)
				{
					jdsp->tmpBuffer[0][sample] = 0.0f;
					jdsp->tmpBuffer[1][sample] = sample == 0 ? 1.0f : 0.0f;
				}
				LiveProgProcess(jdsp, 8);
				for (size_t sample = 0; sample < 8; ++sample)
				{
					assert(jdsp->tmpBuffer[0][sample] == 0.0f);
					assert(fabsf(jdsp->tmpBuffer[1][sample] - leftResponse[sample]) < 0.0001f);
				}
			}
			JamesDSPSetSampleRate(jdsp, 48000.0f, 0);
		}
		if (strstr(argv[i], "gainControl.eel"))
		{
			assert(load_file(jdsp, argv[i]));
			LiveProgEnable(jdsp);
			jdsp->tmpBuffer[0][0] = 1.0f;
			jdsp->tmpBuffer[1][0] = 1.0f;
			LiveProgProcess(jdsp, 1);
			assert(fabsf(jdsp->tmpBuffer[0][0] - 0.398107f) < 0.0001f);
			assert(fabsf(jdsp->tmpBuffer[1][0] - 0.398107f) < 0.0001f);
		}
		if (strstr(argv[i], "swapChannels.eel"))
		{
			assert(load_file(jdsp, argv[i]));
			LiveProgEnable(jdsp);
			jdsp->tmpBuffer[0][0] = 1.0f;
			jdsp->tmpBuffer[1][0] = 2.0f;
			LiveProgProcess(jdsp, 1);
			assert(fabsf(jdsp->tmpBuffer[0][0] - 2.0f) < 0.0001f);
			assert(fabsf(jdsp->tmpBuffer[1][0] - 1.0f) < 0.0001f);
		}
		if (strstr(argv[i], "stereoPhaseInvert.eel"))
		{
			assert(load_file(jdsp, argv[i]));
			LiveProgEnable(jdsp);
			jdsp->tmpBuffer[0][0] = 1.0f;
			jdsp->tmpBuffer[1][0] = -2.0f;
			LiveProgProcess(jdsp, 1);
			assert(fabsf(jdsp->tmpBuffer[0][0] - 1.0f) < 0.0001f);
			assert(fabsf(jdsp->tmpBuffer[1][0] + 2.0f) < 0.0001f);
		}
		if (strstr(argv[i], "liveprogLifecycleDiagnostic.eel"))
		{
			/* A slider update must execute @slider only; lifecycle counters
			 * change only when a subsequent audio block is processed. */
			assert(variable(jdsp, "block_count"));
			assert(variable(jdsp, "sample_count"));
			assert(variable(jdsp, "last_block_size"));
			*variable(jdsp, "block_count") = 0.0f;
			*variable(jdsp, "sample_count") = 0.0f;
			*variable(jdsp, "last_block_size") = 0.0f;
			assert(LiveProgSetVariable(jdsp, "slider1", 2.0f));
			assert(variable(jdsp, "gain") && fabsf(*variable(jdsp, "gain") - 2.0f) < 0.0001f);
			assert(*variable(jdsp, "block_count") == 0.0f);
			assert(*variable(jdsp, "sample_count") == 0.0f);
			jdsp->tmpBuffer[0][0] = 1.0f;
			jdsp->tmpBuffer[1][0] = -1.0f;
			LiveProgProcess(jdsp, 1);
			assert(*variable(jdsp, "block_count") == 1.0f);
			assert(*variable(jdsp, "sample_count") == 1.0f);
			assert(*variable(jdsp, "last_block_size") == 1.0f);
			assert(fabsf(jdsp->tmpBuffer[0][0] - 2.0f) < 0.0001f);
			assert(fabsf(jdsp->tmpBuffer[1][0] + 2.0f) < 0.0001f);
		}
		if (jdsp->liveprogNonFiniteSamples != nonFiniteBefore)
			fprintf(stderr, "%s emitted %llu non-finite output sample(s) before host sanitization\n",
				argv[i], (unsigned long long)(jdsp->liveprogNonFiniteSamples - nonFiniteBefore));
		assert(jdsp->liveprogNonFiniteSamples == nonFiniteBefore);
	}

	JamesDSPFree(jdsp);
	free(jdsp);
	puts("liveprog runtime test passed");
	return 0;
}
