// build_probe / main.c
// DEBUGGER T04 準備フェーズ: ビルド環境健全性検証用スケルトン。
// dummy_intercom rev 0.5 の主要ライブラリ API を参照し、リンク解決を確認する。
// 実動作は行わない(実機なし)。src/ の本実装とは独立。

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"

#include "btstack.h"
#include "classic/btstack_cvsd_plc.h"
#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"

#include "tusb.h"

// ---- 参照用の静的オブジェクト ----
static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_cvsd_plc_state_t cvsd_plc_state;
static int16_t pcm_in[60];
static int16_t pcm_out[60];
static uint8_t upsampled[240];

static void dummy_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)packet_type; (void)channel; (void)packet; (void)size;
}

// core1: TinyUSB タスクループ(spec §7.2)
static void usb_core1_main(void)
{
    tud_init(BOARD_TUD_RHPORT);
    for (;;) {
        tud_task();

        // UAC1 IN へ供給(参照)
        if (tud_audio_mounted()) {
            tud_audio_write(upsampled, sizeof(upsampled));
        }
        // CDC ログ出力(参照)
        if (tud_cdc_connected()) {
            tud_cdc_write_str("probe\r\n");
            tud_cdc_write_flush();
        }
    }
}

// SCO 受信ハンドラ(参照): CVSD/PLC デコード → アップサンプル
static void sco_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;
    if (packet_type != HCI_SCO_DATA_PACKET) return;

    int bad = (packet[1] >> 4) & 3;
    btstack_cvsd_plc_process_data(&cvsd_plc_state, bad != 0, (int16_t *)(packet + 3),
                                  (size - 3) / 2, pcm_out);

    // 2x 線形補間アップサンプル(参照実装。本実装は CODER)
    int n = (size - 3) / 2;
    for (int i = 0; i < n && i < 59; i++) {
        int16_t a = pcm_out[i];
        int16_t b = pcm_out[i + 1];
        ((int16_t *)upsampled)[2 * i]     = a;
        ((int16_t *)upsampled)[2 * i + 1] = (int16_t)((a + b) / 2);
    }
}

int main(void)
{
    stdio_init_all();

    // HCI dump を stdout(CDC/UART)へ: spec §5.4
    hci_dump_init(hci_dump_embedded_stdout_get_instance());

    if (cyw43_arch_init()) {
        printf("cyw43_arch_init failed\n");
        return -1;
    }

    // BTstack コアの初期化(参照)
    l2cap_init();
    sdp_init();
    rfcomm_init();

    // GAP: 名前 / CoD / SSP=NO_IO(spec §5)
    gap_set_local_name("DummyIntercom 00:00:00:00:00:00");
    gap_discoverable_control(1);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_set_class_of_device(0x20040C);

    // HFP-AG(spec §5, §7.3 bt_hf_ag.c 相当)
    uint16_t rfcomm_channel_nr = 1;
    hfp_ag_init(rfcomm_channel_nr);
    hfp_ag_init_supported_features(0x1B);   // WBS ビットは立てない(spec: CVSD 固定)
    hfp_ag_init_codecs(1, (uint8_t[]){ HFP_CODEC_CVSD });
    hfp_ag_init_ag_indicators(0, NULL);
    hfp_ag_init_hf_indicators(0, NULL);
    hfp_ag_init_call_hold_services(0, NULL);

    hfp_ag_set_registration_status(1);
    hfp_ag_set_signal_strength(5);
    hfp_ag_set_roaming_status(0);
    hfp_ag_set_battery_level(5);

    hci_event_callback_registration.callback = &dummy_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    hfp_ag_register_packet_handler(&dummy_packet_handler);
    hci_register_sco_packet_handler(&sco_packet_handler);

    btstack_cvsd_plc_init(&cvsd_plc_state);
    memset(pcm_in, 0, sizeof(pcm_in));

    hci_power_control(HCI_POWER_ON);

    // core1 で TinyUSB(spec §6.3)
    multicore_launch_core1(usb_core1_main);

    btstack_run_loop_execute();
    return 0;
}
