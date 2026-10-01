/*
 * Audio DSP: DC-blocking high-pass + noise gate + polyphase FIR upsampler.
 *
 * Converts the decoded SCO PCM (8 kHz CVSD or 16 kHz mSBC, S16 mono) to the
 * fixed I2S_OUTPUT_RATE_HZ (48 kHz, or 16 kHz with AUDIO_OUT_I2S16K). Runs entirely in the I2S TX task context - never
 * call from the Bluedroid recv callback (ISS-013). See software_design.md 6.1.
 */
#pragma once

#include <stdint.h>

/* Select the input sample rate. hz must be 8000 or 16000; anything else is
 * ignored. Resets all filter state (call on SCO connect / codec change). */
void audio_dsp_set_input_rate(uint32_t hz);

/* Clear every filter/gate/history state. Call on SCO (re)connect. */
void audio_dsp_reset(void);

/* Current upsample factor for the selected input rate (DAC48K: 6 for 8 k, 3 for 16 k; I2S16K: 2 for 8 k, 1 for 16 k). */
uint32_t audio_dsp_upsample_factor(void);

/* Process n_in input samples -> up to n_in * factor output samples at
 * I2S_OUTPUT_RATE_HZ. Writes at most out_cap samples. Returns samples written. */
uint32_t audio_dsp_process(const int16_t *in, uint32_t n_in,
                           int16_t *out, uint32_t out_cap);
