# ESP-IDF patches

These patch the **local ESP-IDF checkout** (`~/esp/esp-idf`), not this repo.
They are lost when ESP-IDF is updated/re-cloned — reapply them.

## esp-idf-v5.4-hfp-bac-lc3.patch

**Problem.** `bta_ag_parse_bac()` in Bluedroid's HFP-AG parses the peer's
`AT+BAC` codec list. On any codec ID it doesn't recognise it does
`return BTA_AG_CODEC_NONE`, throwing away CVSD/mSBC already parsed. An HFP 1.9
Hands-Free unit (the SENA SRL-3) advertises `AT+BAC=1,2,3` where `3` = LC3-SWB,
so the AG sees "no usable codecs" and falls back to **CVSD 8 kHz** even though
both sides support mSBC. Log signature:

```
E BT_APPL: Unknown Codec UUID(3) received
I bt_hf_ag: codec negotiation result: 1 (CVSD)
```

**Fix.** Skip the unknown codec ID and keep parsing the rest of the list.
After the patch the SRL-3 negotiates mSBC and the firmware switches to the
16 kHz wideband path (`bt_hf_ag.c` `ESP_HF_AUDIO_STATE_CONNECTED_MSBC`).

LC3-SWB itself is still unsupported by ESP-IDF v5.4 — this only recovers mSBC.

### Apply

```sh
cd ~/esp/esp-idf
git apply /home/subcon/SOURCE_CODE/dummycom/patches/esp-idf-v5.4-hfp-bac-lc3.patch
# rebuild the firmware (the bt component recompiles)
cd /home/subcon/SOURCE_CODE/dummycom && idf.py build
```

### Revert

```sh
cd ~/esp/esp-idf && git checkout components/bt/host/bluedroid/bta/hf_ag/bta_ag_cmd.c
```

## esp-idf-v5.4-hfp-client-no-post-slc-at.patch

**Problem.** After the SLC is up, Bluedroid's HFP *client* (HF) sends BIA, CCWA,
CMEE, COPS, BTRH and CLIP. A SENA SRL-3 acting as AG (universal intercom) never
answers one of them, so the stack's 30 s AT timer fires and the link is dropped.
Log signature (~30 s after `SLC connected`):

```
E BT_APPL: HFPClient: AT response timeout, disconnecting
```

**Fix.** `send_post_slc_cmd()` returns immediately (these commands only enable
indicator reporting; the receive-only HF role does not need them). Only needed
for `BT_ROLE_HF` builds. Verified on hardware: link stays up, SCO audio received.

### Apply

```sh
cd ~/esp/esp-idf
git apply /home/subcon/SOURCE_CODE/dummycom/patches/esp-idf-v5.4-hfp-client-no-post-slc-at.patch
```
