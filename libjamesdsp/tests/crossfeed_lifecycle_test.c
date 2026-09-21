#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "jdsp_header.h"

extern void JamesDSPProcess(JamesDSPLib *jdsp, size_t n);

static void *process_blocks(void *data)
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
    return NULL;
}

static void *toggle_crossfeed(void *data)
{
    JamesDSPLib *jdsp = (JamesDSPLib *)data;
    for (int iteration = 0; iteration < 256; ++iteration)
    {
        CrossfeedEnable(jdsp, 1);
        CrossfeedChangeMode(jdsp, iteration % 6);
        if ((iteration % 7) == 0)
            CrossfeedDisable(jdsp);
    }
    CrossfeedEnable(jdsp, 1);
    return NULL;
}

int main(void)
{
    JamesDSPLib *jdsp = calloc(1, sizeof(*jdsp));
    assert(jdsp);
    JamesDSPGlobalMemoryAllocation();
    JamesDSPInit(jdsp, 128, 48000.0f);

    char script[] = "@sample\nspl0 = spl0; spl1 = spl1;\n";
    char error[256] = {};
    assert(LiveProgStringParser(jdsp, script, error, sizeof(error)) > 0);

    CrossfeedEnable(jdsp, 1);
    assert(jdsp->advXF.convLong_S_S || jdsp->advXF.convLong_T_S);
    void *longState = jdsp->advXF.convLong_S_S ?
        (void *)jdsp->advXF.convLong_S_S : (void *)jdsp->advXF.convLong_T_S;

    /* Unchanged enable must not rebuild the selected long convolver. */
    CrossfeedEnable(jdsp, 1);
    void *sameLongState = jdsp->advXF.convLong_S_S ?
        (void *)jdsp->advXF.convLong_S_S : (void *)jdsp->advXF.convLong_T_S;
    assert(longState == sameLongState);

    jdsp->advXF.mode = 5;
    jdsp->crossfeedEnabled = 1;
    pthread_t processor, toggler;
    assert(pthread_create(&processor, NULL, process_blocks, jdsp) == 0);
    assert(pthread_create(&toggler, NULL, toggle_crossfeed, jdsp) == 0);
    for (int iteration = 0; iteration < 64; ++iteration)
    {
        jdsp->crossfeedForceRefresh = 1;
        CrossfeedEnable(jdsp, 1);
        CrossfeedChangeMode(jdsp, iteration % 6);
    }
    assert(pthread_join(processor, NULL) == 0);
    assert(pthread_join(toggler, NULL) == 0);

    /* Rate refresh replaces rate-dependent crossfeed state. Repeat both
     * effective-rate paths while enabled, then exercise disable/shutdown. */
    const float rates[] = {44100.0f, 48000.0f, 96000.0f, 48000.0f};
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        JamesDSPSetSampleRate(jdsp, rates[iteration % 4], 0);
        CrossfeedEnable(jdsp, 1);
        assert(jdsp->advXF.convLong_S_S || jdsp->advXF.convLong_T_S);
    }
    CrossfeedDisable(jdsp);
    assert(jdsp->crossfeedEnabled == 0);
    JamesDSPFree(jdsp);
    free(jdsp);
    puts("crossfeed lifecycle test passed");
    return 0;
}
