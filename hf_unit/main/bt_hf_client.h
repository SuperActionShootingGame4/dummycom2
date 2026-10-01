/*
 * GAP + HFP-HF event handling, SCO keepalive / ACL reconnect timers.
 * See software_design.md 13.5.7 / 13.4.
 */
#pragma once

#include <stdbool.h>

/* Register GAP/HF-client callbacks, run esp_hf_client_init(), create timers,
 * init the data path, and dispatch the stack-up work. Call after
 * esp_bluedroid_enable() and bt_app_task_start_up(). */
void bt_hf_client_start(void);

/* Apply discoverable/connectable scan mode based on whether a peer is bonded.
 * Runs on the BtAppT task. */
void bt_hf_client_apply_scan_mode(void);

/* Ask the BtAppT task to erase the stored peer + all bonds and restart.
 * Returns false if the work queue is not up yet (caller should retry).
 * Used by the BOND-CLR button so GAP APIs are never touched from the LED task. */
bool bt_hf_client_request_factory_reset(void);
