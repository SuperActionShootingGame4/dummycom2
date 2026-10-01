// build_probe / usb_descriptors.c
// UAC1 マイク(IN 16k/16bit/mono)+ CDC(ACM ログ)複合デバイスの記述子。
// リンク性/コンパイル検証用。VID/PID/名称は開発用ダミー。

#include "tusb.h"

#define USB_VID   0x2E8A   // Raspberry Pi(開発用)
#define USB_PID   0x000A   // ダミー(頒布時に要取得: spec §6.1 / §13-5)
#define USB_BCD   0x0200

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void)
{
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
enum
{
  ITF_NUM_AUDIO_CONTROL = 0,
  ITF_NUM_AUDIO_STREAMING,
  ITF_NUM_CDC,
  ITF_NUM_CDC_DATA,
  ITF_NUM_TOTAL
};

#define EPNUM_AUDIO_IN    0x81
#define EPNUM_CDC_NOTIF   0x82
#define EPNUM_CDC_OUT     0x03
#define EPNUM_CDC_IN      0x83

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_AUDIO_MIC_ONE_CH_DESC_LEN + TUD_CDC_DESC_LEN)

uint8_t const desc_fs_configuration[] =
{
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 150),

    // UAC1 マイク(1ch): itfnum, stridx, bytes/sample, bits used, EP IN, EP size
    TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX * 8,
        EPNUM_AUDIO_IN, CFG_TUD_AUDIO_EP_SZ_IN),

    // CDC: itfnum, stridx, EP notif, notif size, EP out, EP in, EP size
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index;
  return desc_fs_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_UAC,
  STRID_CDC,
};

char const * string_desc_arr [] =
{
    (const char[]) { 0x09, 0x04 },   // 0: English (0x0409)
    "DummyIntercom",                 // 1: Manufacturer
    "DummyIntercom",                 // 2: Product
    "000000000000",                  // 3: Serial
    "DummyIntercom Mic",             // 4: UAC IN
    "DummyIntercom Log",             // 5: CDC
};

static uint16_t _desc_str[32 + 1];

uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void) langid;
  size_t chr_count;

  if (index == STRID_LANGID) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
    const char * str = string_desc_arr[index];
    chr_count = strlen(str);
    size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
    if (chr_count > max_count) chr_count = max_count;
    for (size_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
  }

  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
