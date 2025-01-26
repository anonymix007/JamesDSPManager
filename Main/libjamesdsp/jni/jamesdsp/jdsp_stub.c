#include <assert.h>

#define TAG "JDSP_Stub"
#include <android/log.h>

#define ALOGF(...) __android_log_print(ANDROID_LOG_FATAL, TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

#include "remote.h"
#include "dsp_capabilities_utils.h"
#include "pd_status_notification.h"

#include "jamesdsp.h"
#include "jdsp_impl.h"

#include "hexagon.h"

#define STATUS_CONTEXT 0x37303061

int pd_status_notifier_callback(void *context, int domain, int session, remote_rpc_status_flags_t status){
    int nErr = AEE_SUCCESS;
    switch (status){
        case FASTRPC_USER_PD_UP:
                ALOGI("PD is up");
                break;
        case FASTRPC_USER_PD_EXIT:
                ALOGI("PD closed");
                break;
        case FASTRPC_USER_PD_FORCE_KILL:
                ALOGI("PD force kill");
                break;
        case FASTRPC_USER_PD_EXCEPTION:
                ALOGI("PD exception");
                break;
        case FASTRPC_DSP_SSR:
               ALOGI("DSP SSR");
               break;
        default:
               nErr = AEE_EBADITEM;
               break;
    }
    return nErr;
}

void EffectDSPMainConstructor(EffectDSPMain *dspmain)
{
    int nErr = pthread_mutex_init(&dspmain->handle_mut, NULL);
    assert(nErr == 0);

    pthread_mutex_lock(&dspmain->handle_mut);
    {
        uint32_t cap = 0;
        if (AEE_SUCCESS != (nErr = get_hex_arch_ver(CDSP_DOMAIN_ID, &cap))) {
            ALOGF("get_hex_arch_ver failed: 0x%x", nErr);
        } else {
            ALOGI("CDSP arch: 0x%08x", cap);
        }
    }

    {
        struct remote_rpc_control_unsigned_module data;
        data.domain = CDSP_DOMAIN_ID;
        data.enable = 1;
        if (AEE_SUCCESS != (nErr = remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE, (void*)&data, sizeof(data)))) {
            ALOGF("remote_session_control failed (unsigned PD): 0x%x", nErr);
        }
    }

    {
        struct remote_rpc_thread_params data;
        data.domain = CDSP_DOMAIN_ID;
        data.prio = -1;
        data.stack_size = 7*1024*1024;;
        if (AEE_SUCCESS != (nErr = remote_session_control(FASTRPC_THREAD_PARAMS, (void*)&data, sizeof(data)))) {
            ALOGF("remote_session_control failed (stack size): 0x%x", nErr);
        }
    }

    if(AEE_SUCCESS != (nErr = request_status_notifications_enable(CDSP_DOMAIN_ID, (void*)STATUS_CONTEXT, pd_status_notifier_callback))) {
        if(nErr != AEE_EUNSUPPORTEDAPI) {
           ALOGE("request_status_notifications_enable failed: 0x%x", nErr);
        }
    }

    if (AEE_SUCCESS == (nErr = jamesdsp_open(jamesdsp_URI CDSP_DOMAIN, &dspmain->handle))) {
        ALOGI("Offloaded effect library initialized: 0x%lx", dspmain->handle);
    } else {
        ALOGF("Failed to initialize offloaded effect library: 0x%x", nErr);
    }
    pthread_mutex_unlock(&dspmain->handle_mut);
}

void EffectDSPMainDestructor(EffectDSPMain *dspmain)
{
    pthread_mutex_lock(&dspmain->handle_mut);
    jamesdsp_close(dspmain->handle);
    pthread_mutex_unlock(&dspmain->handle_mut);
}

int32_t EffectDSPMainCommand(EffectDSPMain *dspmain, uint32_t cmdCode, uint32_t cmdSize, void* pCmdData, uint32_t* pReplySize, void* pReplyData)
{
    pthread_mutex_lock(&dspmain->handle_mut);
    hexagon_effect_config_t qdspCfg;
    effect_config_t *cfg = NULL;
    if (cmdCode == EFFECT_CMD_SET_CONFIG) {
        cfg = (effect_config_t *) pCmdData;
        if (cfg->inputCfg.mask & EFFECT_CONFIG_FORMAT) {
            switch (cfg->inputCfg.format) {
                case AUDIO_FORMAT_PCM_FLOAT:
                case AUDIO_FORMAT_PCM_32_BIT:
                case AUDIO_FORMAT_PCM_8_24_BIT:
                    dspmain->in_size = 4;
                    break;
                case AUDIO_FORMAT_PCM_24_BIT_PACKED:
                    dspmain->in_size = 3;
                    break;
                case AUDIO_FORMAT_PCM_16_BIT:
                    dspmain->in_size = 2;
                    break;
            }
        }
        if (cfg->outputCfg.mask & EFFECT_CONFIG_FORMAT) {
            switch (cfg->outputCfg.format) {
                case AUDIO_FORMAT_PCM_FLOAT:
                case AUDIO_FORMAT_PCM_32_BIT:
                case AUDIO_FORMAT_PCM_8_24_BIT:
                    dspmain->out_size = 4;
                    break;
                case AUDIO_FORMAT_PCM_24_BIT_PACKED:
                    dspmain->out_size = 3;
                    break;
                case AUDIO_FORMAT_PCM_16_BIT:
                    dspmain->out_size = 2;
                    break;
            }
        }
        host2hexagon(cfg, &qdspCfg);
        cmdSize = sizeof(qdspCfg);
        pCmdData = (void *) &qdspCfg;
    } else if (cmdCode == EFFECT_CMD_GET_CONFIG) {
        cfg = (effect_config_t *) pReplyData;
        *pReplySize = sizeof(qdspCfg);
        pReplyData = (void *) &qdspCfg;
    } else if (cmdCode == EFFECT_CMD_DUMP) {
        return -EINVAL;
    }

    int replySize = pReplySize == NULL ? 0 : *pReplySize;

    int32_t result = jamesdsp_command(dspmain->handle, cmdCode, pCmdData, cmdSize, pReplyData, replySize, pReplySize);

    if (cmdCode == EFFECT_CMD_GET_CONFIG) {
        hexagon2host(&qdspCfg, cfg);
        *pReplySize = sizeof(*cfg);
    }

    pthread_mutex_unlock(&dspmain->handle_mut);
    return result;
}

int32_t EffectDSPMainProcess(EffectDSPMain *dspmain, audio_buffer_t *in, audio_buffer_t *out)
{
    pthread_mutex_lock(&dspmain->handle_mut);
    int32_t result = jamesdsp_process(dspmain->handle, in->raw, in->frameCount * dspmain->in_size * 2, out->raw, in->frameCount * dspmain->out_size * 2, in->frameCount);
    pthread_mutex_unlock(&dspmain->handle_mut);
    return result;
}

