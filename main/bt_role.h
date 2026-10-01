/*
 * Role-independent entry points (CR-011). BT_ROLE (app_config.h) selects which
 * of the HFP-AG / HFP-HF implementations is built into the boot path.
 */
#pragma once

#include "app_config.h"

#if BT_ROLE == BT_ROLE_HF
#include "bt_hf_client.h"
#define bt_role_start()                   bt_hf_client_start()
#define bt_role_request_factory_reset()   bt_hf_client_request_factory_reset()
#else
#include "bt_hf_ag.h"
#define bt_role_start()                   bt_hf_ag_start()
#define bt_role_request_factory_reset()   bt_hf_ag_request_factory_reset()
#endif
