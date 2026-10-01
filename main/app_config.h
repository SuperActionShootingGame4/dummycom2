/*
 * Dummy Intercom firmware - compile-time configuration.
 * All GPIO assignments, buffer sizes, timer periods and names live here.
 * See software_design.md 5.2.
 */
#pragma once

/* ---- GPIO (spec 4.3) ---------------------------------------------------- */
#define PIN_I2S_BCLK        26
#define PIN_I2S_WS          25
#define PIN_I2S_DOUT        27
#define PIN_LED_PWR         32
#define PIN_LED_LINK        33
#define PIN_LED_SCO         13
#define PIN_BTN_BOND        14
#define BTN_HOLD_MS         3000

/* ---- Bluetooth (spec 5) ----------------------------------------------- */
#define BT_DEVICE_NAME      "DummyIntercom"
#define BT_COPS_NAME        "DummyNet"
#define SCO_KEEPALIVE_MS    3000
#define ACL_RECONNECT_MS    5000
#define DIAL_FAKE_DELAY_MS  200

/* Class of Device: major = Phone, minor = Smartphone, service = Telephony|Audio
 * (intent-first per U-05 / RQ-10; resulting value ~0x60020C). If SLC fails on
 * real hardware, fall back to the spec literal 0x20040C. */
#define BT_COD_MINOR_SMARTPHONE  0x03
#define BT_COD_MINOR_AV_HANDSFREE 0x02  /* Audio/Video major, Hands-free Device */

/* CR-010 (technical validation, to be hardened later): pair like a typical
 * intercom - SSP off, fixed legacy PIN. Set BT_LEGACY_PAIRING 0 to restore SSP
 * Just Works. BT_COD_PROFILE: 0 = Phone/Smartphone, 1 = AV Hands-free. */
#define BT_LEGACY_PAIRING   1
#define BT_PIN_CODE         "0000"
#define BT_COD_PROFILE      0

/* CR-011: Bluetooth role. AG = phone side (SRL3 "phone pairing" connects as HF).
 * HF = headset side, for SRL3 universal-intercom pairing (SRL3 acts as AG); its
 * CoD is fixed AV/Hands-free (BT_COD_PROFILE applies to the AG role only).
 * Only one role is initialised per boot - Bluedroid cannot run both at once. */
#define BT_ROLE_AG          0
#define BT_ROLE_HF          1
#define BT_ROLE             BT_ROLE_HF

/* ---- Audio (spec 6) -------------------------------------------------- */
/* SCO PCM arrives at 8 kHz (CVSD) or 16 kHz (mSBC/WBS). It is always upsampled
 * to a fixed I2S_OUTPUT_RATE_HZ so the PCM5102A PLL runs in a comfortable range
 * and its reconstruction filter has margin (spec 6 / ISS-017). */
#define PCM_SAMPLE_RATE_HZ       8000   /* default SCO PCM rate (CVSD); mSBC = 16000 */
#define PCM_BYTES_PER_SAMPLE     2
#define PCM_TX_BLOCK_US          7500   /* outgoing_data_ready period, aligned to eSCO Tesco */
#define JITTER_RB_BYTES          4096   /* ~256 ms @ 8 kHz / ~128 ms @ 16 kHz mono S16 */
#define JITTER_TARGET_BYTES      960    /* 60 ms @ 8 kHz (shorten to 640 = 40 ms for lower latency) */
#define JITTER_PREFILL_BYTES     480    /* 30 ms @ 8 kHz buffered before playback starts */
#define I2S_CHUNK_MONO_SAMPLES   64     /* TX task processing unit, in SCO-rate samples (8 ms @ 8 kHz) */
/* CR-012: output mode. I2S16K (default) = 16 kHz mono digital I2S master feeding
 * the second ESP32 (hf_unit) over 3 wires. DAC48K = previous 48 kHz stereo
 * output for a PCM5102A DAC. */
#define AUDIO_OUT_DAC48K         0
#define AUDIO_OUT_I2S16K         1
#define AUDIO_OUT_MODE           AUDIO_OUT_I2S16K

#if AUDIO_OUT_MODE == AUDIO_OUT_I2S16K
#define I2S_OUTPUT_RATE_HZ       16000  /* fixed I2S sample rate (mono, Philips, left slot) */
#define I2S_UP_MAX               2      /* max upsample factor: 8 k->16 k = 2 (16 k = 1) */
#define I2S_OUT_CHANNELS         1
#define I2S_DMA_DESC_NUM         6
#define I2S_DMA_FRAME_NUM        64     /* 64 frames @ 16 kHz = 4 ms/desc, x6 = ~24 ms DMA */
#else
#define I2S_OUTPUT_RATE_HZ       48000  /* fixed I2S / DAC sample rate */
#define I2S_UP_MAX               6      /* max upsample factor: 8 k->48 k = 6 (16 k->48 k = 3) */
#define I2S_OUT_CHANNELS         2
#define I2S_DMA_DESC_NUM         8
#define I2S_DMA_FRAME_NUM        128    /* 128 frames @ 48 kHz = 2.67 ms/desc, x8 = ~21 ms DMA */
#endif
#define RMS_LOG_PERIOD_US        1000000

/* ---- Audio DSP (spec 6 / ISS-015 / ISS-017) -------------------------- *
 * Applied in the I2S TX task (NOT the recv callback - keeps ISS-013 intact).
 * Order per input sample: DC-block high-pass -> noise gate -> polyphase FIR
 * interpolation to I2S_OUTPUT_RATE_HZ. */
/* One-pole DC blocker. The topology has a mild broadband droop of a*2/(1+a)
 * (e.g. -0.08 dB at 30 Hz corner, -0.4 dB at 120 Hz), so keep the corner low
 * for a transparent rumble/DC cut; use a biquad if you need a real 100+ Hz HPF.
 * 0 disables entirely. */
#define AUDIO_HPF_CUTOFF_HZ      30.0f
/* Linear make-up gain applied after the HPF (before the gate). The SRL3 mic path is
 * very quiet (speech rms ~10, hand-tap ~250 of 32767), so 25 = +28 dB. 1.0 = off.
 * Output is clipped to int16, so lower it if loud peaks distort. */
#define AUDIO_MAKEUP_GAIN        25.0f
#define AUDIO_GATE_ENABLE        0      /* 1 = downward expander on idle hiss; 0 = passthrough (default:
                                        * validate the resampler/codec first, then enable if hiss bothers) */
#define AUDIO_GATE_OPEN_LEVEL    600.0f /* post-HPF |sample| envelope that opens the gate (~ -35 dBFS) */
#define AUDIO_GATE_FLOOR         0.10f  /* closed-state gain (-20 dB); 0.0 = hard gate, 1.0 = disable */
#define AUDIO_GATE_HOLD_MS       250    /* hold fully open this long after the last above-threshold sample */
#define AUDIO_GATE_ENV_REL_MS    15.0f  /* envelope release time constant (attack is instant) */
#define AUDIO_GATE_ATTACK_MS     0.8f   /* gain slew when opening (fast: do not clip speech onsets) */
#define AUDIO_GATE_RELEASE_MS    80.0f  /* gain slew when closing (gentle: no pumping) */

/* CR-014: HFP speaker volume (AT+VGS, 0..15) sent to the AG (SRL3) after SLC and
 * SCO setup. The AG scales its SCO output by it; 15 = loudest. -1 = do not send. */
#define HF_SPK_VOLUME            15
#if HF_SPK_VOLUME > 15 || HF_SPK_VOLUME < -1
#error "HF_SPK_VOLUME must be -1 (disabled) or 0..15"
#endif

/* AT+VGM (0..15) sent alongside AT+VGS. -1 = do not send. Whether the SRL3 honours
 * it for its own mic path: tested on hardware, SRL3 ignores it (no rms change). */
#define HF_MIC_VOLUME            -1
#if HF_MIC_VOLUME > 15 || HF_MIC_VOLUME < -1
#error "HF_MIC_VOLUME must be -1 (disabled) or 0..15"
#endif

/* ---- LEDs ----------------------------------------------------------- */
#define LED_TASK_PERIOD_MS       50
#define SCO_ERR_UNDERRUN_THRESH  5      /* underruns/sec above this -> SCO LED error blink */

/* ---- NVS ----------------------------------------------------------- */
#define NVS_NS             "dummyicom"
#define NVS_KEY_PEER_BDA   "peer_bda"   /* blob, 6 bytes */
