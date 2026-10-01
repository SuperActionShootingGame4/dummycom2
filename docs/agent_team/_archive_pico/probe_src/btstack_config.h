// build_probe / btstack_config.h
// ベース: pico-sdk test/kitchen_sink/btstack_config.h(pico_w BT 既知動作構成)
// 追加: dummy_intercom spec rev 0.5 §5.1 に沿った HFP-AG / SCO-over-HCI / AT ログ設定
// ※ ENABLE_CLASSIC / ENABLE_BLE は pico_btstack_classic / pico_btstack_ble リンクで自動定義されるため
//    ここでは定義しない(pico_btstack/doc.h の指示)。

#ifndef _PICO_BTSTACK_BTSTACK_CONFIG_H
#define _PICO_BTSTACK_BTSTACK_CONFIG_H

// BTstack features that can be enabled
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_CENTRAL
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP

// --- dummy_intercom 固有(受信専用 HFP-AG / SCO) ---
#define ENABLE_SCO_OVER_HCI
#define ENABLE_HFP_AT_MESSAGES          // 未知 AT を UNAT イベントで受けてログ化(spec R6)

// BTstack configuration. buffers, sizes, ...
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
#define MAX_NR_AVDTP_CONNECTIONS 1
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 1
#define MAX_NR_AVRCP_CONNECTIONS 2
#define MAX_NR_BNEP_CHANNELS 1
#define MAX_NR_BNEP_SERVICES 1
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES  2
#define MAX_NR_GATT_CLIENTS 1
#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_HFP_CONNECTIONS 1
#define MAX_NR_L2CAP_CHANNELS  4
#define MAX_NR_L2CAP_SERVICES  3
#define MAX_NR_RFCOMM_CHANNELS 1
#define MAX_NR_RFCOMM_MULTIPLEXERS 1
#define MAX_NR_RFCOMM_SERVICES 1
#define MAX_NR_SERVICE_RECORD_ITEMS 4
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 1
#define MAX_NR_LE_DEVICE_DB_ENTRIES 4

#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3

// HCI Controller to Host Flow Control
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

// NVM configuration
#define NVM_NUM_DEVICE_DB_ENTRIES 16
#define NVM_NUM_LINK_KEYS 16

#define MAX_ATT_DB_SIZE 512

#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT

// map btstack_assert onto Pico SDK assert()
#define HCI_RESET_RESEND_TIMEOUT_MS 1000

#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE

#endif
