/*
 * ADC1 continuous-mode (DMA) audio capture: 32 kHz oversample -> boxcar
 * decimation (/4 = 8 kHz CVSD, /2 = 16 kHz mSBC) -> DC blocker -> handoff to
 * bt_data_path. See software_design.md 13.5.5.
 */
#pragma once

#include <stdint.h>

/* Start ADC1 continuous mode @ ADC_SAMPLE_RATE_HZ and the adc_capture task.
 * Call once, during boot (before or after Bluetooth bring-up - independent). */
void adc_capture_init(void);

/* Set the decimation factor: ADC_DECIM_FACTOR_CVSD (4, ->8kHz) or
 * ADC_DECIM_FACTOR_MSBC (2, ->16kHz). Call from the AUDIO_STATE_EVT handler
 * (BtAppT context). Safe to call from any task - simple volatile write. */
void adc_capture_set_decimation(uint32_t factor);

/* Count of adc_continuous internal-pool overflows (on_pool_ovf events) since
 * boot. ISS-034: diagnostic counter only, mirrors bt_data_path's underrun/
 * overrun counters - not yet wired to an LED state. */
uint32_t adc_capture_overrun_count(void);
