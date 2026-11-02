#pragma once
#include "audio_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUD_ENABLED           1
#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE)
#define CFG_TUSB_DEBUG            0
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN        __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE    64

#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

// ---- UAC2 speaker (host -> device) ----
#define CFG_TUD_AUDIO                              1
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN              AUDIO_FUNC_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT              1
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ           64

#define CFG_TUD_AUDIO_ENABLE_EP_OUT                1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX AUDIO_BYTES
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX         2
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX         AUDIO_EP_SZ_MAX
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ      (8 * AUDIO_EP_SZ_MAX)

#ifdef __cplusplus
}
#endif
