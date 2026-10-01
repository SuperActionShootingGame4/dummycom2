/*
 * NVS-backed peer bond storage (peer = Insta360 X4). See software_design.md 13.5.4.
 */
#pragma once

#include <stdbool.h>
#include "esp_bt_defs.h"

/* Open the NVS namespace and load the stored peer BDA (if any). */
void storage_init(void);

/* True if a peer BDA has been stored. */
bool storage_has_peer(void);

/* Copy the stored peer BDA (6 bytes) into out. Undefined if !storage_has_peer(). */
void storage_get_peer(esp_bd_addr_t out);

/* Persist the peer BDA. No-op if it already matches the stored value. */
void storage_save_peer(const esp_bd_addr_t bda);

/* Erase the stored peer BDA and every Bluedroid bond. Caller restarts after. */
void storage_factory_reset(void);
