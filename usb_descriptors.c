#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "audio_config.h"

enum { ITF_NUM_AUDIO_CONTROL = 0, ITF_NUM_AUDIO_STREAMING, ITF_NUM_TOTAL };
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + AUDIO_FUNC_DESC_LEN)

// ---------------------------------------------------------------- device
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
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

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

// ---------------------------------------------------------------- UAC2: USB streaming -> speaker
// Clock source (programmable) -> Input terminal (USB streaming) -> Output terminal (speaker)
#define TUD_AUDIO_DAC_DESCRIPTOR(_itfnum, _stridx, _epout)                                             \
    TUD_AUDIO_DESC_IAD(_itfnum, 0x02, 0x00),                                                           \
    TUD_AUDIO_DESC_STD_AC(_itfnum, 0x00, _stridx),                                                     \
    TUD_AUDIO_DESC_CS_AC(0x0200, 0x01 /*desktop speaker*/,                                             \
        TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_INPUT_TERM_LEN + TUD_AUDIO_DESC_OUTPUT_TERM_LEN,   \
        0x00),                                                                                         \
    /* clock id, attr=internal programmable, ctrl=freq r/w + validity r, assoc, str */                 \
    TUD_AUDIO_DESC_CLK_SRC(ENTITY_CLOCK, 0x03, 0x07, 0x00, 0x00),                                      \
    /* id, type=USB streaming 0x0101, assoc, clk, nch, chcfg=FL|FR, chnames, ctrl, str */              \
    TUD_AUDIO_DESC_INPUT_TERM(ENTITY_INPUT_TERM, 0x0101, 0x00, ENTITY_CLOCK, 0x02, 0x00000003, 0x00,   \
        0x0000, 0x00),                                                                                 \
    /* id, type=desktop speaker 0x0301, assoc, src, clk, ctrl, str */                                  \
    TUD_AUDIO_DESC_OUTPUT_TERM(ENTITY_OUTPUT_TERM, 0x0301, 0x00, ENTITY_INPUT_TERM, ENTITY_CLOCK,      \
        0x0000, 0x00),                                                                                 \
    /* AS interface alt 0 (no bandwidth) and alt 1 (streaming) */                                      \
    TUD_AUDIO_DESC_STD_AS_INT((uint8_t)((_itfnum) + 1), 0x00, 0x00, 0x00),                             \
    TUD_AUDIO_DESC_STD_AS_INT((uint8_t)((_itfnum) + 1), 0x01, 0x01, 0x00),                             \
    TUD_AUDIO_DESC_CS_AS_INT(ENTITY_INPUT_TERM, 0x00, 0x01 /*FORMAT_TYPE_I*/, 0x00000001 /*PCM*/,      \
        0x02, 0x00000003, 0x00),                                                                       \
    TUD_AUDIO_DESC_TYPE_I_FORMAT(AUDIO_BYTES, AUDIO_BITS),                                             \
    /* ISO OUT, ADAPTIVE (device follows host rate) */                                                 \
    TUD_AUDIO_DESC_STD_AS_ISO_EP(_epout,                                                               \
        (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ADAPTIVE | TUSB_ISO_EP_ATT_DATA),           \
        AUDIO_EP_SZ_MAX, 0x01),                                                                        \
    TUD_AUDIO_DESC_CS_AS_ISO_EP(0x00, 0x00, 0x00, 0x0000)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_AUDIO_DAC_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, EP_AUDIO_OUT)
};

_Static_assert(sizeof(desc_configuration) == CONFIG_TOTAL_LEN,
               "AUDIO_FUNC_DESC_LEN in audio_config.h does not match the descriptor");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

// ---------------------------------------------------------------- strings
static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},   // 0: English
    "DIY Audio",                  // 1: manufacturer
    "Pico2 USB I2S DAC",          // 2: product
    NULL,                         // 3: serial (from flash unique id)
    "Pico2 USB I2S DAC",          // 4: audio interface
};

static uint16_t desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;
    static char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

    if (index == 0) {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *str = string_desc_arr[index];
        if (index == 3) {
            pico_get_unique_board_id_string(serial, sizeof(serial));
            str = serial;
        }
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) desc_str[1 + i] = str[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
