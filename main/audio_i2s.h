/*
 * I2S0 master TX @ I2S_OUTPUT_RATE_HZ / 16-bit + jitter buffer. See software_design.md 5.5.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Bring up I2S0 TX, the jitter ring buffer and the TX task. Output starts silent. */
void audio_i2s_init(void);

/* Set the SCO PCM sample rate feeding the jitter buffer: 8000 (CVSD) or
 * 16000 (mSBC/WBS). Call on SCO connect, before audio_i2s_set_sco_active(true). */
void audio_i2s_set_input_rate(uint32_t hz);

/* Push decoded S16 mono PCM (at the rate set by audio_i2s_set_input_rate) into
 * the jitter buffer. Non-blocking. Called from the Bluedroid recv data callback
 * (BTC context) only. */
void audio_i2s_write(const uint8_t *pcm_s16_mono, uint32_t len);

/* Gate playback. false -> TX task outputs silence. */
void audio_i2s_set_sco_active(bool active);

/* Drop buffered audio and re-arm prefill. Call on SCO connect. */
void audio_i2s_reset(void);

/* Statistics for the SCO LED / diagnostics. */
uint32_t audio_i2s_underrun_count(void);
uint32_t audio_i2s_overrun_count(void);
