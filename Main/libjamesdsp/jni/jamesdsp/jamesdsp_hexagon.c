#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "HAP_farf.h"
#include "jamesdsp.h"

#include "jdsp_impl.h"

int jamesdsp_open(const char*uri, remote_handle64* handle) {
    HAP_setFARFRuntimeLoggingParams(0x1f, NULL, 0);
    JamesDSPGlobalMemoryAllocation();
    EffectDSPMain *dspmain = calloc(sizeof(EffectDSPMain), 1);
    *handle = (remote_handle64) dspmain;
    assert(*handle);
    EffectDSPMainConstructor(dspmain);
    return 0;
}

int jamesdsp_close(remote_handle64 handle) {
    if (handle) {
        EffectDSPMainDestructor((EffectDSPMain *) handle);
        free(handle);
    }
    JamesDSPGlobalMemoryDeallocation();
    return 0;
}

int32_t jamesdsp_command(remote_handle64 handle, uint32_t cmdCode, const uint8_t *pCmdData, int cmdSize, uint8_t *pReplyData, int replySize, uint32_t *pReplySize) {
    return EffectDSPMainCommand((EffectDSPMain *) handle, cmdCode, cmdSize, pCmdData, pReplySize, pReplyData);
}

int32_t jamesdsp_process(remote_handle64 handle, const uint8_t *inPcm, int inPcmLen, uint8_t *outPcm, int outPcmLen, uint32_t frameCount) {
    audio_buffer_t in = {
        .frameCount = frameCount,
        .raw = inPcm,
    };
    audio_buffer_t out = {
        .frameCount = frameCount,
        .raw = outPcm,
    };
    return EffectDSPMainProcess((EffectDSPMain *) handle, &in, &out);
}
