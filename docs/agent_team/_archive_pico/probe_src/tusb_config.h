// build_probe / tusb_config.h
// dummy_intercom spec rev 0.5 §6: UAC1(IN, 16kHz/16bit/mono マイク)+ CDC(ログ)複合デバイス。
// ※ TinyUSB の MIC ワンチャンネル記述子マクロを使用。UAC1/UAC2 の最終選定は DESIGNER/CODER 側。
//    本 probe はリンク性検証が目的。

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_FULL_SPEED
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
#define CFG_TUD_AUDIO            1
#define CFG_TUD_CDC              1
#define CFG_TUD_MSC              0
#define CFG_TUD_HID              0
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR          0

// CDC FIFO
#define CFG_TUD_CDC_RX_BUFSIZE   256
#define CFG_TUD_CDC_TX_BUFSIZE   256

//--------------------------------------------------------------------
// AUDIO CLASS DRIVER CONFIGURATION (UAC1 マイク: 16kHz / 16bit / mono)
//--------------------------------------------------------------------
#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE                 16000
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN                    TUD_AUDIO_MIC_ONE_CH_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT                    1
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ                 64

#define CFG_TUD_AUDIO_ENABLE_EP_IN                       1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX       2
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX               1
#define CFG_TUD_AUDIO_EP_SZ_IN                           TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX               CFG_TUD_AUDIO_EP_SZ_IN
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ            (TUD_OPT_HIGH_SPEED ? 8 : 1) * CFG_TUD_AUDIO_EP_SZ_IN

#ifdef __cplusplus
}
#endif

#endif
