/*
 * HFP-AG Voice-over-HCI data path: recv/send callbacks + uplink silence.
 * See software_design.md 5.6 / 6.5 / 6.6.
 */
#pragma once

/* Register the recv/send data callbacks and create the timers.
 * Must be called after esp_bluedroid_enable() and esp_hf_ag_init(). */
void bt_data_path_init(void);

/* SCO connected/disconnected: start/stop the outgoing_data_ready timer. */
void bt_data_path_on_audio_connected(void);
void bt_data_path_on_audio_disconnected(void);
