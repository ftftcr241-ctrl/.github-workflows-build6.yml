#pragma once

// ---------------- Pins ----------------
#define I2S_DATA_PIN   18   // DIN
#define I2S_BCLK_PIN   16   // BCK ; LRCK = BCLK_PIN + 1 = GP17

// ---------------- Audio format ----------------
#define AUDIO_BYTES        3        // 3 = 24 bit, 2 = 16 bit
#define AUDIO_BITS         (AUDIO_BYTES * 8)
#define AUDIO_MAX_RATE     96000
// Full-speed isochronous max packet: (nominal + 1 frame) * bytes * channels
#define AUDIO_EP_SZ_MAX    ((AUDIO_MAX_RATE / 1000 + 1) * AUDIO_BYTES * 2)

// Total length of the audio function descriptor (checked by static assert in usb_descriptors.c)
#define AUDIO_FUNC_DESC_LEN 118

// ---------------- USB ids ----------------
#define USB_VID  0xCafe          // TODO: replace with your own VID/PID for products
#define USB_PID  0x4010
#define EP_AUDIO_OUT 0x01

// UAC2 entity ids
#define ENTITY_INPUT_TERM   0x01
#define ENTITY_OUTPUT_TERM  0x03
#define ENTITY_CLOCK        0x04
