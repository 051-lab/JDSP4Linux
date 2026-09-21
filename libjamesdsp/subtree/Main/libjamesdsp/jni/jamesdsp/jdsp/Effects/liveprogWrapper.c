void NSEEL_HOSTSTUB_EnterMutex() { }
void NSEEL_HOSTSTUB_LeaveMutex() { }
#include "../jdsp_header.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t liveProgCompileMutex = PTHREAD_MUTEX_INITIALIZER;
static void LiveProgDestroyState(LiveProg *pg);

static LiveProg *LiveProgCurrent(const JamesDSPLib *jdsp)
{
	return __atomic_load_n(&jdsp->liveProgCurrent, __ATOMIC_ACQUIRE);
}

static void LiveProgPublish(JamesDSPLib *jdsp, LiveProg *candidate)
{
	LiveProg *previous = __atomic_exchange_n(&jdsp->liveProgCurrent, candidate, __ATOMIC_ACQ_REL);
	if (previous)
	{
		previous->retiredNext = jdsp->liveProgRetired;
		jdsp->liveProgRetired = previous;
	}
}

static void LiveProgDestroyRetired(JamesDSPLib *jdsp)
{
	LiveProg *retired = jdsp->liveProgRetired;
	jdsp->liveProgRetired = 0;
	while (retired)
	{
		LiveProg *next = retired->retiredNext;
		LiveProgDestroyState(retired);
		if (retired != &jdsp->eel)
			free(retired);
		retired = next;
	}
}

#ifdef JDSP_TEST_HOOKS
#include <stdatomic.h>
#include <unistd.h>

static _Atomic int liveProgLoadDelayMs;
static _Atomic int liveProgLoadStarted;
static _Atomic int liveProgLoadActive;
static _Atomic int liveProgLoadMaxActive;

void JamesDSPSetLiveProgLoadDelayForTests(int delayMs)
{
	atomic_store(&liveProgLoadDelayMs, delayMs > 0 ? delayMs : 0);
	atomic_store(&liveProgLoadStarted, 0);
	atomic_store(&liveProgLoadActive, 0);
	atomic_store(&liveProgLoadMaxActive, 0);
}

int JamesDSPLiveProgLoadStartedForTests(void)
{
	return atomic_load(&liveProgLoadStarted);
}

int JamesDSPLiveProgMaxConcurrentLoadsForTests(void)
{
	return atomic_load(&liveProgLoadMaxActive);
}

LiveProg *JamesDSPGetCurrentLiveProgForTests(JamesDSPLib *jdsp)
{
	return jdsp ? LiveProgCurrent(jdsp) : 0;
}
#endif

enum
{
	LIVEPROG_SECTION_NONE = -1,
	LIVEPROG_SECTION_INIT,
	LIVEPROG_SECTION_SLIDER,
	LIVEPROG_SECTION_BLOCK,
	LIVEPROG_SECTION_SAMPLE,
	LIVEPROG_SECTION_COUNT,
	LIVEPROG_SECTION_OTHER,
	LIVEPROG_SECTION_MALFORMED = -2
};

typedef struct
{
	const char *start;
	size_t length;
	int present;
} LiveProgSourceSection;

static int LiveProgSectionAtLine(const char *line, const char *lineEnd)
{
	while (line < lineEnd && (*line == ' ' || *line == '\t'))
		line++;
	if (line >= lineEnd || *line != '@')
		return LIVEPROG_SECTION_NONE;

	const char *name = ++line;
	while (line < lineEnd && (isalnum((unsigned char)*line) || *line == '_'))
		line++;
	const size_t nameLength = (size_t)(line - name);
	if (!nameLength)
		return LIVEPROG_SECTION_NONE;

	while (line < lineEnd && (*line == ' ' || *line == '\t' || *line == '\r'))
		line++;
	if (line < lineEnd && !(line + 1 < lineEnd && line[0] == '/' && line[1] == '/'))
		return LIVEPROG_SECTION_MALFORMED;

	if (nameLength == 4 && !strncmp(name, "init", nameLength))
		return LIVEPROG_SECTION_INIT;
	if (nameLength == 6 && !strncmp(name, "slider", nameLength))
		return LIVEPROG_SECTION_SLIDER;
	if (nameLength == 5 && !strncmp(name, "block", nameLength))
		return LIVEPROG_SECTION_BLOCK;
	if (nameLength == 6 && !strncmp(name, "sample", nameLength))
		return LIVEPROG_SECTION_SAMPLE;
	return LIVEPROG_SECTION_OTHER;
}

static int LiveProgSplitSource(const char *source, LiveProgSourceSection sections[LIVEPROG_SECTION_COUNT])
{
	memset(sections, 0, sizeof(LiveProgSourceSection) * LIVEPROG_SECTION_COUNT);
	const char *end = source + strlen(source);
	const char *line = source;
	int currentSection = LIVEPROG_SECTION_NONE;

	while (line < end)
	{
		const char *newLine = memchr(line, '\n', (size_t)(end - line));
		const char *lineEnd = newLine ? newLine : end;
		const int section = LiveProgSectionAtLine(line, lineEnd);
		if (section == LIVEPROG_SECTION_MALFORMED || section == LIVEPROG_SECTION_OTHER)
			return -8;
		if (section != LIVEPROG_SECTION_NONE)
		{
			if (currentSection >= 0 && currentSection < LIVEPROG_SECTION_COUNT)
				sections[currentSection].length = (size_t)(line - sections[currentSection].start);
			currentSection = LIVEPROG_SECTION_NONE;

			if (section >= 0 && section < LIVEPROG_SECTION_COUNT)
			{
				if (sections[section].present)
					return -6;
				sections[section].present = 1;
				sections[section].start = newLine ? newLine + 1 : end;
				currentSection = section;
			}
		}
		line = newLine ? newLine + 1 : end;
	}

	if (currentSection >= 0 && currentSection < LIVEPROG_SECTION_COUNT)
		sections[currentSection].length = (size_t)(end - sections[currentSection].start);
	return sections[LIVEPROG_SECTION_SAMPLE].present ? 1 : -2;
}

static char *LiveProgCopySection(const LiveProgSourceSection *section)
{
	if (!section->present)
		return 0;
	char *copy = (char*)calloc(section->length + 1, sizeof(char));
	if (copy && section->length)
		memcpy(copy, section->start, section->length);
	return copy;
}

static void LiveProgClearCode(LiveProg *pg)
{
	if (pg->codehandleProcess)
		NSEEL_code_free(pg->codehandleProcess);
	if (pg->codehandleBlock)
		NSEEL_code_free(pg->codehandleBlock);
	if (pg->codehandleSlider)
		NSEEL_code_free(pg->codehandleSlider);
	if (pg->codehandleInit)
		NSEEL_code_free(pg->codehandleInit);
	pg->codehandleInit = 0;
	pg->codehandleSlider = 0;
	pg->codehandleBlock = 0;
	pg->codehandleProcess = 0;
}

static void LiveProgDestroyState(LiveProg *pg)
{
	if (pg->vm)
	{
		LiveProgClearCode(pg);
		NSEEL_VM_free(pg->vm);
	}
	while (pg->hostOverrides)
	{
		LiveProgVariableOverride *next = pg->hostOverrides->next;
		free(pg->hostOverrides);
		pg->hostOverrides = next;
	}
	memset(pg, 0, sizeof(*pg));
}

static int LiveProgInitializeState(LiveProg *pg, float sampleRate)
{
	memset(pg, 0, sizeof(*pg));
	pg->active = 1;
	pg->vm = NSEEL_VM_alloc();
	if (!pg->vm)
		return 0;
	pg->vmFs = NSEEL_VM_regvar(pg->vm, "srate");
	pg->samplesBlock = NSEEL_VM_regvar(pg->vm, "samplesblock");
	pg->input1 = NSEEL_VM_regvar(pg->vm, "spl0");
	pg->input2 = NSEEL_VM_regvar(pg->vm, "spl1");
	if (!pg->vmFs || !pg->samplesBlock || !pg->input1 || !pg->input2)
	{
		LiveProgDestroyState(pg);
		return 0;
	}
	*pg->vmFs = sampleRate;
	*pg->samplesBlock = 0;
	return 1;
}

void LiveProgConstructor(JamesDSPLib *jdsp)
{
	LiveProgInitializeState(&jdsp->eel, jdsp->fs);
	jdsp->eel.retiredNext = 0;
	jdsp->liveProgRetired = 0;
	__atomic_store_n(&jdsp->liveProgCurrent, &jdsp->eel, __ATOMIC_RELEASE);
}

void LiveProgDestructor(JamesDSPLib *jdsp)
{
	LiveProg *current = __atomic_exchange_n(&jdsp->liveProgCurrent, 0, __ATOMIC_ACQ_REL);
	if (current && current != &jdsp->eel)
	{
		LiveProgDestroyState(current);
		free(current);
	}
	LiveProgDestroyRetired(jdsp);
	if (current == &jdsp->eel || jdsp->eel.vm)
		LiveProgDestroyState(&jdsp->eel);
}

void LiveProgEnable(JamesDSPLib *jdsp)
{
	if (!jdsp)
		return;
	processing_pause(jdsp);
	LiveProg *pg = LiveProgCurrent(jdsp);
	if (pg && pg->vmFs && pg->compileSucessfully)
	{
		*pg->vmFs = jdsp->fs;
		jdsp->liveprogEnabled = 1;
	}
	else
		jdsp->liveprogEnabled = 0;
	processing_resume(jdsp);
}

void LiveProgDisable(JamesDSPLib *jdsp)
{
	if (!jdsp)
		return;
	processing_pause(jdsp);
	jdsp->liveprogEnabled = 0;
	processing_resume(jdsp);
}

void LiveProgRefreshSampleRatePaused(JamesDSPLib *jdsp, float sampleRate)
{
	LiveProg *pg = jdsp ? LiveProgCurrent(jdsp) : 0;
	if (!pg || !pg->vm || !pg->compileSucessfully)
		return;
	if (pg->vmFs)
		*pg->vmFs = sampleRate;
	if (pg->codehandleInit)
		NSEEL_code_execute(pg->codehandleInit);
	for (LiveProgVariableOverride *override = pg->hostOverrides; override; override = override->next)
	{
		float *variable = NSEEL_VM_getvar(pg->vm, override->name);
		if (variable)
			*variable = override->value;
	}
	if (pg->codehandleSlider)
		NSEEL_code_execute(pg->codehandleSlider);
	for (LiveProgVariableOverride *override = pg->hostOverrides; override; override = override->next)
	{
		float *variable = NSEEL_VM_getvar(pg->vm, override->name);
		if (variable)
			override->value = *variable;
	}
}

void LiveProgRefreshSampleRate(JamesDSPLib *jdsp, float sampleRate)
{
	if (!jdsp)
		return;
	processing_pause(jdsp);
	LiveProgRefreshSampleRatePaused(jdsp, sampleRate);
	processing_resume(jdsp);
}

static int LiveProgLoadCode(LiveProg *pg, float sampleRate, const char *codeTextInit, const char *codeTextSlider,
	const char *codeTextBlock, const char *codeTextProcess)
{
#ifdef JDSP_TEST_HOOKS
	const int delayMs = atomic_load(&liveProgLoadDelayMs);
	if (delayMs > 0)
	{
		atomic_store(&liveProgLoadStarted, 1);
		const int active = atomic_fetch_add(&liveProgLoadActive, 1) + 1;
		int previous = atomic_load(&liveProgLoadMaxActive);
		while (active > previous &&
			!atomic_compare_exchange_weak(&liveProgLoadMaxActive, &previous, active))
			;
		usleep((useconds_t)delayMs * 1000U);
		atomic_fetch_sub(&liveProgLoadActive, 1);
		atomic_store(&liveProgLoadStarted, 0);
	}
#endif
	pg->compileSucessfully = 0;
	compileContext *ctx = (compileContext*)pg->vm;
	LiveProgClearCode(pg);
	NSEEL_VM_freevars(pg->vm);
	NSEEL_init_memRegion(pg->vm);
	memset(ctx->ram_state, 0, sizeof(ctx->ram_state));
	pg->vmFs = NSEEL_VM_regvar(pg->vm, "srate");
	pg->samplesBlock = NSEEL_VM_regvar(pg->vm, "samplesblock");
	pg->input1 = NSEEL_VM_regvar(pg->vm, "spl0");
	pg->input2 = NSEEL_VM_regvar(pg->vm, "spl1");
	if (!pg->vmFs || !pg->samplesBlock || !pg->input1 || !pg->input2)
		return -7;
	*pg->vmFs = sampleRate;
	*pg->samplesBlock = 0;
	ctx->functions_common = 0;

	if (codeTextInit && *codeTextInit)
	{
		pg->codehandleInit = NSEEL_code_compile_ex(pg->vm, codeTextInit, 0,
			NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS | NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS_RESET);
		if (!pg->codehandleInit)
			return -1;
	}
	if (codeTextSlider && *codeTextSlider)
	{
		pg->codehandleSlider = NSEEL_code_compile(pg->vm, codeTextSlider, 0);
		if (!pg->codehandleSlider)
			return -4;
	}
	if (codeTextBlock && *codeTextBlock)
	{
		pg->codehandleBlock = NSEEL_code_compile(pg->vm, codeTextBlock, 0);
		if (!pg->codehandleBlock)
			return -5;
	}
	pg->codehandleProcess = NSEEL_code_compile(pg->vm, codeTextProcess, 0);
	if (!pg->codehandleProcess)
		return -3;

	if (pg->codehandleInit)
		NSEEL_code_execute(pg->codehandleInit);
	if (pg->codehandleSlider)
		NSEEL_code_execute(pg->codehandleSlider);
	pg->compileSucessfully = 1;
	return 1;
}

const char* checkErrorCode(int errCode)
{
	switch (errCode)
	{
	case -1:
		return "Syntax error at @init section";
	case -2:
		return "@sample section not found";
	case -3:
		return "Syntax error at @sample section";
	case -4:
		return "Syntax error at @slider section";
	case -5:
		return "Syntax error at @block section";
	case -6:
		return "Duplicate LiveProg section";
	case -7:
		return "Failed to allocate LiveProg source sections";
	case -8:
		return "Unsupported or malformed LiveProg section";
	default:
		return "No syntax errors detected";
	}
}

int LiveProgStringParser(JamesDSPLib *jdsp, char *eelCode, char *errorBuffer, size_t errorBufferSize)
{
	if (!jdsp || !eelCode)
		return -7;
	if (errorBuffer && errorBufferSize)
		errorBuffer[0] = '\0';
	LiveProgSourceSection sections[LIVEPROG_SECTION_COUNT];
	int errorMsg = LiveProgSplitSource(eelCode, sections);
	char *codeText[LIVEPROG_SECTION_COUNT] = { 0 };
	if (errorMsg > 0)
	{
		for (int i = 0; i < LIVEPROG_SECTION_COUNT; i++)
		{
			codeText[i] = LiveProgCopySection(&sections[i]);
			if (sections[i].present && !codeText[i])
			{
				errorMsg = -7;
				break;
			}
		}
		if (errorMsg > 0)
		{
			float candidateRate;
			jdsp_lock(jdsp);
			candidateRate = jdsp->fs;
			jdsp_unlock(jdsp);
			pthread_mutex_lock(&liveProgCompileMutex);
			LiveProg *candidate = (LiveProg*)calloc(1, sizeof(*candidate));
			if (!candidate || !LiveProgInitializeState(candidate, candidateRate))
			{
				if (candidate)
					LiveProgDestroyState(candidate);
				free(candidate);
				errorMsg = -7;
			}
			else
			{
				errorMsg = LiveProgLoadCode(candidate, candidateRate,
					codeText[LIVEPROG_SECTION_INIT], codeText[LIVEPROG_SECTION_SLIDER],
					codeText[LIVEPROG_SECTION_BLOCK], codeText[LIVEPROG_SECTION_SAMPLE]);
				if (errorMsg > 0)
				{
					jdsp_lock(jdsp);
					if (jdsp->fs != candidateRate)
					{
						errorMsg = -7;
						if (errorBuffer && errorBufferSize)
							snprintf(errorBuffer, errorBufferSize,
								"sample rate changed while compiling candidate");
					}
					else
					{
					LiveProg *previous = LiveProgCurrent(jdsp);
					candidate->active = previous ? previous->active : 0;
					candidate->retiredNext = 0;
					LiveProgPublish(jdsp, candidate);
					jdsp_unlock(jdsp);
					}
					if (errorMsg <= 0)
						jdsp_unlock(jdsp);
				}
				else
				{
					const char *error = NSEEL_code_getcodeerror(candidate->vm);
					if (error && errorBuffer && errorBufferSize)
						snprintf(errorBuffer, errorBufferSize, "%s", error);
				}
				if (errorMsg <= 0)
				{
					LiveProgDestroyState(candidate);
					free(candidate);
				}
			}
			pthread_mutex_unlock(&liveProgCompileMutex);
		}
	}
	for (int i = 0; i < LIVEPROG_SECTION_COUNT; i++)
		free(codeText[i]);
	return errorMsg;
}

int LiveProgSetVariable(JamesDSPLib *jdsp, const char *name, float value)
{
	if (!jdsp || !name || !*name || !isfinite(value))
		return 0;
	const size_t nameLength = strlen(name);
	if (nameLength > NSEEL_MAX_VARIABLE_NAMELEN ||
		!(isalpha((unsigned char)name[0]) || name[0] == '_'))
		return 0;
	for (size_t i = 1; i < nameLength; i++)
		if (!(isalnum((unsigned char)name[i]) || name[i] == '_'))
			return 0;
	processing_pause(jdsp);
	LiveProg *pg = LiveProgCurrent(jdsp);
	if (!pg)
	{
		processing_resume(jdsp);
		return 0;
	}
	float *variable = pg->vm && pg->compileSucessfully ? NSEEL_VM_getvar(pg->vm, name) : 0;
	if (!variable)
	{
		processing_resume(jdsp);
		return 0;
	}
	if (!pg->codehandleSlider)
	{
		/* Legacy programs derive coefficients in @init.  The host must reload
		 * the persisted source instead of treating this assignment as live-safe. */
		*variable = value;
		processing_resume(jdsp);
		return 0;
	}
	LiveProgVariableOverride *override = pg->hostOverrides;
	while (override && strcmp(override->name, name) != 0)
		override = override->next;
	if (!override)
	{
		override = (LiveProgVariableOverride*)calloc(1, sizeof(*override));
		if (!override)
		{
			processing_resume(jdsp);
			return 0;
		}
		memcpy(override->name, name, nameLength + 1);
		override->next = pg->hostOverrides;
		pg->hostOverrides = override;
	}
	*variable = value;
	NSEEL_code_execute(pg->codehandleSlider);
	override->value = *variable;
	processing_resume(jdsp);
	return 1;
}

void LiveProgProcess(JamesDSPLib *jdsp, size_t n)
{
	LiveProg *eel = LiveProgCurrent(jdsp);
	if (!eel)
		return;
	if (eel->compileSucessfully && eel->active)
	{
		*eel->samplesBlock = (float)n;
		if (eel->codehandleBlock)
			NSEEL_code_execute(eel->codehandleBlock);
		for (size_t i = 0; i < n; i++)
		{
			*eel->input1 = jdsp->tmpBuffer[0][i];
			*eel->input2 = jdsp->tmpBuffer[1][i];
			NSEEL_code_execute(eel->codehandleProcess);
			if (isinf((float)*eel->input1) || isnan((float)*eel->input1))
			{
				++jdsp->liveprogNonFiniteSamples;
				jdsp->tmpBuffer[0][i] = 0;
			}
			else
				jdsp->tmpBuffer[0][i] = (float)*eel->input1;

			if (isinf((float)*eel->input2) || isnan((float)*eel->input2))
			{
				++jdsp->liveprogNonFiniteSamples;
				jdsp->tmpBuffer[1][i] = 0;
			}
			else
				jdsp->tmpBuffer[1][i] = (float)*eel->input2;
		}
	}
}
