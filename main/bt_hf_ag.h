/*
 * GAP + HFP-AG event handling, call faking, SCO keepalive / reconnect.
 * See software_design.md 5.7 / 4.2 / 6.4.
 */
#pragma once

#include <stdbool.h>

/* Register GAP/HF callbacks, run esp_hf_ag_init(), create timers, and dispatch
 * the stack-up work. Call after esp_bluedroid_enable() and bt_app_task_start_up(). */
void bt_hf_ag_start(void);

/* Apply scan mode: connectable always; discoverable whenever no SLC is up.
 * Runs on the BtAppT task. */
void bt_hf_ag_apply_scan_mode(void);

/* Ask the BtAppT task to erase the stored peer + all bonds and restart.
 * Returns false if the work queue is not up yet (caller should retry).
 * Used by the BOND-CLR button so GAP APIs are never touched from the LED task. */
bool bt_hf_ag_request_factory_reset(void);
