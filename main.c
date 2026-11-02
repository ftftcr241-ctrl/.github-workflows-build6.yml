// USB Audio Class 2 (stereo, 24-bit) -> I2S master, Raspberry Pi Pico 2 (RP2350)
//
//  * USB: TinyUSB UAC2, adaptive ISO OUT endpoint, rates 44.1/48/88.2/96 kHz
//  * I2S: PIO state machine, fed by two chained DMA channels (ping-pong)
//  * Clock sync: a small software "PLL" trims the PIO fractional divider so
//    the TinyUSB FIFO fill level stays constant (no glitches / drift).

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "tusb.h"
#include "audio_config.h"
#include "i2s_out.pio.h"

#define FRAMES_PER_BUF   96          // 2 ms @ 48 kHz per DMA buffer
#define TARGET_FILL_MS   4           // desired USB FIFO fill level
#define PLL_KP           1.0e-5f     // fractional rate change per frame of error
#define PLL_MAX          1.5e-3f     // clamp: +-1500 ppm

static const uint32_t k_rates[] = {44100, 48000, 88200, 96000};
#define N_RATES (sizeof(k_rates) / sizeof(k_rates[0]))

// ------------------------------------------------------------------ state
static PIO  pio = pio0;
static uint sm;
static int  dma_ch[2];
static uint32_t dma_buf[2][FRAMES_PER_BUF * 2];       // L,R,L,R ... (32-bit words)
static volatile uint32_t pending;                     // bit i = dma_buf[i] needs refill

static uint32_t cur_rate = 48000;
static volatile bool streaming;
static bool  primed;
static float nominal_div, lvl_avg, div_resid;
static uint32_t underruns;

// ------------------------------------------------------------------ helpers
static inline uint32_t target_frames(void) { return cur_rate * TARGET_FILL_MS / 1000; }

static void apply_div(float div) {
    float q = div * 256.0f + div_resid;       // error-diffusion so the average is exact
    uint32_t qi = (uint32_t)q;
    div_resid = q - (float)qi;
    pio_sm_set_clkdiv_int_frac(pio, sm, (uint16_t)(qi >> 8), (uint8_t)(qi & 0xFF));
}

static void set_rate(uint32_t rate) {
    cur_rate    = rate;
    nominal_div = (float)clock_get_hz(clk_sys) / ((float)rate * 128.0f);
    lvl_avg     = (float)target_frames();
    div_resid   = 0.0f;
    primed      = false;
    apply_div(nominal_div);
}

// ------------------------------------------------------------------ I2S / DMA
static void __isr __time_critical_func(dma_irq_handler)(void) {
    for (int i = 0; i < 2; i++) {
        if (dma_channel_get_irq1_status(dma_ch[i])) {
            dma_channel_acknowledge_irq1(dma_ch[i]);
            dma_channel_set_read_addr(dma_ch[i], dma_buf[i], false);   // re-arm, chain keeps running
            __atomic_fetch_or(&pending, 1u << i, __ATOMIC_RELEASE);
        }
    }
}

static void i2s_init(void) {
    sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &i2s_out_program);

    pio_sm_config c = i2s_out_program_get_default_config(offset);
    sm_config_set_out_pins(&c, I2S_DATA_PIN, 1);
    sm_config_set_sideset_pins(&c, I2S_BCLK_PIN);
    sm_config_set_out_shift(&c, false, true, 32);          // MSB first, autopull 32
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    pio_gpio_init(pio, I2S_DATA_PIN);
    pio_gpio_init(pio, I2S_BCLK_PIN);
    pio_gpio_init(pio, I2S_BCLK_PIN + 1);
    pio_sm_set_consecutive_pindirs(pio, sm, I2S_DATA_PIN, 1, true);
    pio_sm_set_consecutive_pindirs(pio, sm, I2S_BCLK_PIN, 2, true);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_exec(pio, sm, pio_encode_set(pio_x, 30));
    set_rate(cur_rate);

    // two chained channels, each plays one buffer then triggers the other
    dma_ch[0] = dma_claim_unused_channel(true);
    dma_ch[1] = dma_claim_unused_channel(true);
    for (int i = 0; i < 2; i++) {
        dma_channel_config dc = dma_channel_get_default_config(dma_ch[i]);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, true);
        channel_config_set_write_increment(&dc, false);
        channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));
        channel_config_set_chain_to(&dc, dma_ch[i ^ 1]);
        dma_channel_configure(dma_ch[i], &dc, &pio->txf[sm], dma_buf[i], FRAMES_PER_BUF * 2, false);
        dma_channel_set_irq1_enabled(dma_ch[i], true);
    }
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    dma_channel_start(dma_ch[0]);
    pio_sm_set_enabled(pio, sm, true);
}

// ------------------------------------------------------------------ USB -> DMA buffer
static void pll_update(uint32_t avail_frames) {
    lvl_avg += ((float)avail_frames - lvl_avg) * (1.0f / 32.0f);
    float err = lvl_avg - (float)target_frames();        // >0: FIFO too full -> consume faster
    float adj = err * PLL_KP;
    if (adj >  PLL_MAX) adj =  PLL_MAX;
    if (adj < -PLL_MAX) adj = -PLL_MAX;
    apply_div(nominal_div * (1.0f - adj));
}

static void fill_buffer(uint32_t *dst) {
    const uint32_t fb   = AUDIO_BYTES * 2;
    const uint32_t need = FRAMES_PER_BUF * fb;
    static uint8_t tmp[FRAMES_PER_BUF * AUDIO_BYTES * 2];

    if (!streaming) { primed = false; goto silence; }

    uint32_t avail  = tud_audio_available();
    uint32_t target = target_frames() * fb;

    if (!primed) {                       // pre-buffer before starting
        if (avail < target) goto silence;
        primed = true;
    }
    if (avail < need) { primed = false; underruns++; goto silence; }

    // too much queued (e.g. after rate change): drop the excess to bound latency
    while (avail > target * 3 + need) {
        uint32_t drop = (avail - target) / fb * fb;
        if (drop > sizeof(tmp)) drop = sizeof(tmp);
        tud_audio_read(tmp, (uint16_t)drop);
        avail -= drop;
    }

    tud_audio_read(tmp, need);
    for (uint32_t i = 0; i < FRAMES_PER_BUF * 2; i++) {
#if AUDIO_BYTES == 3
        const uint8_t *p = &tmp[i * 3];
        dst[i] = ((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24);
#else
        const uint8_t *p = &tmp[i * 2];
        dst[i] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 24);
#endif
    }
    pll_update(avail / fb);
    return;

silence:
    memset(dst, 0, FRAMES_PER_BUF * 2 * sizeof(uint32_t));
}

static void i2s_service(void) {
    for (int i = 0; i < 2; i++) {
        uint32_t m = 1u << i;
        if (pending & m) {
            __atomic_fetch_and(&pending, ~m, __ATOMIC_ACQ_REL);
            fill_buffer(dma_buf[i]);
        }
    }
}

// ------------------------------------------------------------------ UAC2 control requests
typedef struct TU_ATTR_PACKED { int32_t bCur; } cur4_t;
typedef struct TU_ATTR_PACKED { uint8_t bCur; } cur1_t;
typedef struct TU_ATTR_PACKED {
    uint16_t wNumSubRanges;
    struct TU_ATTR_PACKED { int32_t bMin, bMax, bRes; } r[N_RATES];
} range4_t;

#define UAC2_REQ_CUR        0x01
#define UAC2_REQ_RANGE      0x02
#define UAC2_CS_SAM_FREQ    0x01
#define UAC2_CS_CLK_VALID   0x02

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    uint8_t ctrlSel  = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);
    if (entityID != ENTITY_CLOCK) return false;

    if (ctrlSel == UAC2_CS_SAM_FREQ) {
        if (p_request->bRequest == UAC2_REQ_CUR) {
            cur4_t cur = { (int32_t)tu_htole32(cur_rate) };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur, sizeof(cur));
        } else if (p_request->bRequest == UAC2_REQ_RANGE) {
            range4_t rg = { .wNumSubRanges = tu_htole16(N_RATES) };
            for (uint32_t i = 0; i < N_RATES; i++) {
                rg.r[i].bMin = (int32_t)tu_htole32(k_rates[i]);
                rg.r[i].bMax = (int32_t)tu_htole32(k_rates[i]);
                rg.r[i].bRes = 0;
            }
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &rg, sizeof(rg));
        }
    } else if (ctrlSel == UAC2_CS_CLK_VALID && p_request->bRequest == UAC2_REQ_CUR) {
        cur1_t v = { 1 };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &v, sizeof(v));
    }
    return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
    (void)rhport;
    uint8_t ctrlSel  = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);
    if (entityID == ENTITY_CLOCK && ctrlSel == UAC2_CS_SAM_FREQ &&
        p_request->bRequest == UAC2_REQ_CUR && p_request->wLength == sizeof(cur4_t)) {
        uint32_t rate = (uint32_t)tu_le32toh(((cur4_t *)buf)->bCur);
        for (uint32_t i = 0; i < N_RATES; i++) {
            if (k_rates[i] == rate) { set_rate(rate); return true; }
        }
    }
    return false;
}

// alt setting 0 -> stop, alt setting 1 -> stream
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport; (void)p_request;
    streaming = false;
    return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport;
    uint8_t alt = TU_U16_LOW(p_request->wValue);
    if (alt != 0) {
        tud_audio_clear_ep_out_ff();
        primed = false;
        lvl_avg = (float)target_frames();
        streaming = true;
    } else {
        streaming = false;
    }
    return true;
}

// ------------------------------------------------------------------ main
int main(void) {
    set_sys_clock_khz(153600, true);     // 153.6 MHz = integer-friendly for 48k family (falls back gracefully)
    i2s_init();
    tusb_init();

    while (true) {
        tud_task();
        i2s_service();
    }
}
