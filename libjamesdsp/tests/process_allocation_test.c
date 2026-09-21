#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <math.h>

#include "jdsp_header.h"

extern void JamesDSPProcess(JamesDSPLib *jdsp, size_t n);
extern void JamesDSPSetBufferAllocationFailureForTests(int fail);
extern void JamesDSPSetRefreshCallCountForTests(size_t count);
extern size_t JamesDSPGetRefreshCallCountForTests(void);

static int track_allocations;
static size_t process_allocations;
static size_t lock_calls;
static long long lock_wait_ns;
static long long max_lock_wait_ns;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
int __real_pthread_mutex_lock(pthread_mutex_t *mutex);

int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    struct timespec before = {};
    struct timespec after = {};
    if (track_allocations)
        clock_gettime(CLOCK_MONOTONIC, &before);
    const int result = __real_pthread_mutex_lock(mutex);
    if (track_allocations)
    {
        clock_gettime(CLOCK_MONOTONIC, &after);
        const long long elapsed = (after.tv_sec - before.tv_sec) * 1000000000LL +
                                  after.tv_nsec - before.tv_nsec;
        ++lock_calls;
        lock_wait_ns += elapsed;
        if (elapsed > max_lock_wait_ns)
            max_lock_wait_ns = elapsed;
    }
    return result;
}

void *__wrap_malloc(size_t size)
{
    if (track_allocations)
        ++process_allocations;
    return __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
    if (track_allocations)
        ++process_allocations;
    return __real_calloc(count, size);
}

void *__wrap_realloc(void *pointer, size_t size)
{
    if (track_allocations)
        ++process_allocations;
    return __real_realloc(pointer, size);
}

int main(int argc, char **argv)
{
    JamesDSPGlobalMemoryAllocation();
    JamesDSPLib *jdsp = calloc(1, sizeof(*jdsp));
    assert(jdsp != NULL);
    JamesDSPInit(jdsp, 128, 48000.0f);

    char script[] = "@init\ngain = 1;\n@sample\nspl0 *= gain; spl1 *= gain;\n";
    char error[256] = {};
    assert(LiveProgStringParser(jdsp, script, error, sizeof(error)) > 0);
    LiveProgEnable(jdsp);
    jdsp->liveprogEnabled = 1;

    float *prepared_buffer = jdsp->tmpBuffer[0];
    const size_t prepared_block_size = jdsp->blockSizeMax;
    JamesDSPSetBufferAllocationFailureForTests(1);
    JamesDSPReallocateBlock(jdsp, 4096);
    assert(jdsp->tmpBuffer[0] == prepared_buffer);
    assert(jdsp->blockSizeMax == prepared_block_size);
    float input_left[4096] = {};
    float input_right[4096] = {};
    float output_left[4096];
    float output_right[4096];
    for (size_t sample = 0; sample < 4096; ++sample)
    {
        output_left[sample] = 1.0f;
        output_right[sample] = -1.0f;
    }
    JamesDSPSetBufferAllocationFailureForTests(1);
    jdsp->processFloatDeinterleaved(jdsp, input_left, input_right,
                                    output_left, output_right, 4096);
    assert(jdsp->tmpBuffer[0] == prepared_buffer);
    assert(jdsp->blockSizeMax == prepared_block_size);
    for (size_t sample = 0; sample < 4096; ++sample)
        assert(output_left[sample] == 0.0f && output_right[sample] == 0.0f);
    JamesDSPSetBufferAllocationFailureForTests(0);
    process_allocations = 0;
    for (size_t sample = 0; sample < 4096; ++sample)
    {
        output_left[sample] = 1.0f;
        output_right[sample] = -1.0f;
    }
    jdsp->processFloatDeinterleaved(jdsp, input_left, input_right,
                                    output_left, output_right, 4096);
    assert(jdsp->tmpBuffer[0] == prepared_buffer);
    assert(jdsp->blockSizeMax == prepared_block_size);
    assert(process_allocations == 0);
    for (size_t sample = 0; sample < 4096; ++sample)
        assert(output_left[sample] == 0.0f && output_right[sample] == 0.0f);
    jdsp->tmpBuffer[0][0] = 0.25f;
    jdsp->tmpBuffer[1][0] = -0.25f;
    JamesDSPProcess(jdsp, prepared_block_size);
    assert(isfinite(jdsp->tmpBuffer[0][0]) && isfinite(jdsp->tmpBuffer[1][0]));

    const size_t prepared_runtime_block_size = jdsp->blockSize;
    JamesDSPSetBufferAllocationFailureForTests(1);
    JamesDSPReallocateBlock(jdsp, 1024);
    assert(jdsp->blockSize == prepared_runtime_block_size);
    JamesDSPSetBufferAllocationFailureForTests(0);

    /* A smaller-than-prepared quantum must not enter the refresh/setup path
     * from the processing callback. */
    JamesDSPSetRefreshCallCountForTests(0);
    jdsp->processFloatDeinterleaved(jdsp, input_left, input_right,
                                    output_left, output_right, 64);
    assert(JamesDSPGetRefreshCallCountForTests() == 0);

    JamesDSPSetRefreshCallCountForTests(0);
    JamesDSPReallocateBlock(jdsp, 1024);
    assert(jdsp->blockSizeMax == 1024);
    assert(jdsp->blockSize == 1024);
    assert(JamesDSPGetRefreshCallCountForTests() > 0);
    if (argc > 1 && strcmp(argv[1], "compressor") == 0)
    {
        double frequencies[7] = {95.0, 200.0, 400.0, 800.0, 1600.0, 3400.0, 7500.0};
        double gains[7] = {0};
        CompressorSetParam(jdsp, 0.22f, 2, 0, 1);
        CompressorSetGain(jdsp, frequencies, gains, 1);
        CompressorEnable(jdsp, 1);
    }
    if (argc > 1 && strcmp(argv[1], "bass") == 0)
    {
        BassBoostSetParam(jdsp, 5.0f);
        BassBoostEnable(jdsp);
    }
    if (argc > 1 && strcmp(argv[1], "reverb") == 0)
        ReverbEnable(jdsp);
    if (argc > 1 && strcmp(argv[1], "stereo") == 0)
        StereoEnhancementEnable(jdsp);
    if (argc > 1 && strcmp(argv[1], "tube") == 0)
        VacuumTubeEnable(jdsp);
    if (argc > 1 && strcmp(argv[1], "crossfeed") == 0)
        CrossfeedEnable(jdsp, 1);

    process_allocations = 0;
    track_allocations = 1;
    for (int block = 0; block < 256; ++block)
    {
        for (size_t sample = 0; sample < 128; ++sample)
        {
            jdsp->tmpBuffer[0][sample] = 0.125f;
            jdsp->tmpBuffer[1][sample] = -0.125f;
        }
        JamesDSPProcess(jdsp, 128);
    }
    track_allocations = 0;
    assert(process_allocations == 0);
    assert(lock_calls > 0);
    assert(max_lock_wait_ns < 10000000LL);

    JamesDSPFree(jdsp);
    free(jdsp);
    JamesDSPGlobalMemoryDeallocation();
    puts("steady-state process allocation test passed");
    return 0;
}
