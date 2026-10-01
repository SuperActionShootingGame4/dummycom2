/*
 * HFP-HF Voice-over-HCI data path: ADC/I2S-sourced PCM -> SCO transmit ring
 * buffer -> send_cb. recv (downlink, X4 -> HF) is unused and dropped.
 * See software_design.md 13.5.6.
 */
#pragma once

#include <stdint.h>

/* Register the recv/send data callbacks and create the tx-ready timer.
 * Must be called after esp_bluedroid_enable() and esp_hf_client_init(). */
void bt_data_path_init(void);

/* SCO connected (AUDIO_STATE_EVT CONNECTED/CONNECTED_MSBC): flush the tx
 * ring buffer and clear the prefill flag. */
void bt_data_path_reset(void);

/* SCO connected/disconnected: start/stop the outgoing_data_ready timer. */
void bt_data_path_on_audio_connected(void);
void bt_data_path_on_audio_disconnected(void);

/* SCO rate (8000/16000) for the I2S-mode prefill / silence-fill limits (CR-012).
 * Call before bt_data_path_reset(). No-op in AUDIO_IN_ADC mode. */
void bt_data_path_set_rate(uint32_t hz);

/* Approximate bytes queued in the tx ring buffer (drift-correction input). */
uint32_t bt_data_path_fill_bytes(void);

/* Producer: called from the adc_capture / i2s_capture task (task context, non-blocking).
 * mono, 16-bit PCM at the current SCO rate. */
void bt_data_path_write_pcm(const int16_t *mono, uint32_t n_samples);

/* Stats for the LEDs' SCO_ERR decision. */
uint32_t bt_data_path_underrun_count(void);
uint32_t bt_data_path_overrun_count(void);
