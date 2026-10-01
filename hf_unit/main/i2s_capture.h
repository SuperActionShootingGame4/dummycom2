/*
 * I2S slave RX capture (CR-012): 16 kHz / 16-bit / mono / Philips / left slot
 * from the first ESP32 (I2S master) -> [CVSD: half-band decimation to 8 kHz]
 * -> +/-1 sample drift correction -> bt_data_path. See software_design.md 15.6.2.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Start I2S0 RX slave and the i2s_capture task. Call once during boot. On
 * failure it logs and returns; Bluetooth keeps working without audio input. */
void i2s_capture_init(void);

/* SCO PCM rate: 8000 (CVSD) or 16000 (mSBC). Call from BtAppT; a volatile
 * write, applied at the next chunk boundary. The I2S rate stays 16 kHz. */
void i2s_capture_set_rate(uint32_t sco_hz);

/* true while SCO is connected. While false the DMA data is read and dropped. */
void i2s_capture_set_active(bool on);

/* Diagnostics. */
uint32_t i2s_capture_clock_lost_count(void);   /* BCLK loss events */
uint32_t i2s_capture_drift_ins_count(void);    /* 1-sample insertions */
uint32_t i2s_capture_drift_del_count(void);    /* 1-sample deletions */
