#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "jdsp_header.h"

static _Atomic int probe_enabled;
static _Atomic unsigned long allocations;
static _Atomic unsigned long releases;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);

void *__wrap_malloc(size_t size)
{
	void *result = __real_malloc(size);
	if (atomic_load(&probe_enabled))
		atomic_fetch_add(&allocations, 1);
	return result;
}

void *__wrap_calloc(size_t count, size_t size)
{
	void *result = __real_calloc(count, size);
	if (atomic_load(&probe_enabled))
		atomic_fetch_add(&allocations, 1);
	return result;
}

void *__wrap_realloc(void *pointer, size_t size)
{
	void *result = __real_realloc(pointer, size);
	if (atomic_load(&probe_enabled))
		atomic_fetch_add(&allocations, 1);
	return result;
}

void __wrap_free(void *pointer)
{
	if (atomic_load(&probe_enabled))
		atomic_fetch_add(&releases, 1);
	__real_free(pointer);
}

static void reset_probe(void)
{
	atomic_store(&allocations, 0);
	atomic_store(&releases, 0);
}

int main(void)
{
	JamesDSPLib dsp = {};
	JamesDSPGlobalMemoryAllocation();
	JamesDSPInit(&dsp, 64, 48000.0f);
	char error[256] = {};
	char source[] = "@init\ngain = 1;\n@sample\nspl0 *= gain; spl1 *= gain;\n";
	assert(LiveProgStringParser(&dsp, source, error, sizeof(error)) > 0);
	LiveProgEnable(&dsp);

	reset_probe();
	atomic_store(&probe_enabled, 1);
	for (size_t i = 0; i < 64; ++i)
	{
		dsp.tmpBuffer[0][i] = 0.25f;
		dsp.tmpBuffer[1][i] = -0.25f;
	}
	JamesDSPProcess(&dsp, 64);
	atomic_store(&probe_enabled, 0);
	assert(atomic_load(&allocations) == 0);
	assert(atomic_load(&releases) == 0);

	reset_probe();
	atomic_store(&probe_enabled, 1);
	JamesDSPReallocateBlock(&dsp, 257);
	atomic_store(&probe_enabled, 0);
	assert(atomic_load(&allocations) > 0);
	printf("callback allocations=%lu frees=%lu; quantum-preparation allocations=%lu\n",
		0UL, 0UL, atomic_load(&allocations));

	JamesDSPFree(&dsp);
	JamesDSPGlobalMemoryDeallocation();
	puts("liveprog callback allocation probe passed");
	return 0;
}
