/*
 * Emu68 PiStorm Classic USB mouse host POC1.
 * TinyUSB is used only for host core + HID + Synopsys DWC2 HCD.
 * DWC2 power-up and register base are taken from the already-proven Emu68 UVC POC.
 * No hub, CDC, MSC, device stack or DMA.
 */
#include <stdint.h>
#include "tusb.h"
#include "class/hid/hid.h"
#include "class/hid/hid_host.h"
#include "support.h"

#if defined(PISTORM_CLASSIC)

/*
 * Keep all USB mouse diagnostics in the source, but disable their runtime
 * instrumentation for the release-style milestone.
 * Set to 1 temporarily when low-level DWC2/TinyUSB tracing is needed again.
 */
#ifndef EMU68_USBMOUSE_DIAGNOSTICS
#define EMU68_USBMOUSE_DIAGNOSTICS 0
#endif

extern uint32_t set_power_state(uint32_t device_id, uint32_t state);

volatile uint8_t emu68_usbmouse_x;
volatile uint8_t emu68_usbmouse_y;

/* HID Boot Mouse buttons, active here as logical booleans (1 = pressed).
 * They are converted to the Amiga's active-low hardware lines in vectors.c. */
volatile uint8_t emu68_usbmouse_left;
volatile uint8_t emu68_usbmouse_right;

/*
 * USB transport and Amiga-visible motion intentionally run at different rates.
 *
 * - TinyUSB/DWC2 is serviced by CPU2 at ~1 kHz.
 * - HID deltas are accumulated here at full USB report rate.
 * - Every 20 ms (50 Hz), half-scale motion is published into the virtual
 *   JOY0DAT counters.  Integer remainder is retained, so slow 1-count motion
 *   is never discarded.
 *
 * Only CPU2/bootstrap USB service touches these private accumulators.
 * vectors.c only reads emu68_usbmouse_x/y.
 */
static int32_t usbmouse_raw_x;
static int32_t usbmouse_raw_y;
static uint64_t usbmouse_publish_last;
static uint64_t usbmouse_publish_ticks;
volatile uint8_t emu68_usbmouse_mounted;

/* Splash diagnostics. */
volatile uint8_t  emu68_usbmouse_host_init_ok;
volatile uint8_t  emu68_usbmouse_host_mode;
volatile uint8_t  emu68_usbmouse_port_connected;
volatile uint8_t  emu68_usbmouse_enum_mounted;
volatile uint8_t  emu68_usbmouse_enum_addr;
volatile uint32_t emu68_usbmouse_report_count;
volatile int8_t   emu68_usbmouse_last_dx;
volatile int8_t   emu68_usbmouse_last_dy;
volatile uint32_t emu68_usbmouse_gsnpsid;

volatile uint8_t  emu68_usbmouse_reset_started;
volatile uint8_t  emu68_usbmouse_reset_ended;
volatile uint32_t emu68_usbmouse_setup_count;
volatile uint8_t  emu68_usbmouse_setup_dev;
volatile uint8_t  emu68_usbmouse_setup_bmreq;
volatile uint8_t  emu68_usbmouse_setup_breq;
volatile uint16_t emu68_usbmouse_setup_wvalue;
volatile uint16_t emu68_usbmouse_setup_windex;
volatile uint16_t emu68_usbmouse_setup_wlength;
volatile uint32_t emu68_usbmouse_hcint_count;
volatile uint8_t  emu68_usbmouse_last_ch;
volatile uint32_t emu68_usbmouse_last_hcint;
volatile uint32_t emu68_usbmouse_last_hcchar;
volatile uint32_t emu68_usbmouse_last_hctsiz;
volatile uint32_t emu68_usbmouse_hprt;
volatile uint32_t emu68_usbmouse_gintsts;
volatile uint32_t emu68_usbmouse_gintmsk;
volatile uint32_t emu68_usbmouse_hnptxsts;
volatile uint32_t emu68_usbmouse_nptxfe_count;
volatile uint32_t emu68_usbmouse_nptxfe_gintsts;
volatile uint32_t emu68_usbmouse_nptxfe_gintmsk;
volatile uint32_t emu68_usbmouse_hc0_hcchar;
volatile uint32_t emu68_usbmouse_hc0_hctsiz;
volatile uint32_t emu68_usbmouse_hc0_hcintmsk;

#define EMU68_USBMOUSE_HC_TRACE_MAX 8
volatile uint32_t emu68_usbmouse_hc_trace_count;
volatile uint32_t emu68_usbmouse_hc_trace_int[EMU68_USBMOUSE_HC_TRACE_MAX];
volatile uint32_t emu68_usbmouse_hc_trace_char[EMU68_USBMOUSE_HC_TRACE_MAX];
volatile uint32_t emu68_usbmouse_hc_trace_size[EMU68_USBMOUSE_HC_TRACE_MAX];
volatile uint32_t emu68_usbmouse_hc_trace_split[EMU68_USBMOUSE_HC_TRACE_MAX];
volatile uint32_t emu68_usbmouse_hc_trace_mask[EMU68_USBMOUSE_HC_TRACE_MAX];
volatile uint8_t  emu68_usbmouse_hc_trace_root_speed[EMU68_USBMOUSE_HC_TRACE_MAX];
volatile uint8_t  emu68_usbmouse_hc_trace_dev_speed[EMU68_USBMOUSE_HC_TRACE_MAX];
volatile uint32_t emu68_usbmouse_hc0_hcsplt;

#define USB2_BASE     0xf2980000UL
#define USB_GUSBCFG   0x00c
#define USB_GINTSTS   0x014
#define USB_GINTMSK   0x018
#define USB_HNPTXSTS  0x02c
#define USB_HC0_BASE  0x500
#define USB_HCCHAR0   (USB_HC0_BASE + 0x00)
#define USB_HCSPLT0   (USB_HC0_BASE + 0x04)
#define USB_HCINTMSK0 (USB_HC0_BASE + 0x0c)
#define USB_HCTSIZ0   (USB_HC0_BASE + 0x10)
#define USB_GSNPSID   0x040
#define USB_HPRT      0x440
#define USB_CURMODE_HOST (1U << 0)
#define USB_HPRT_PCSTS    (1U << 0)

static inline uint32_t usb_rd(uint32_t off)
{
    return LE32(*(volatile uint32_t *)(USB2_BASE + off));
}

static void emu68_usbmouse_diag_sample(void)
{
#if EMU68_USBMOUSE_DIAGNOSTICS
    emu68_usbmouse_host_mode =
        (usb_rd(USB_GINTSTS) & USB_CURMODE_HOST) ? 1U : 0U;
    emu68_usbmouse_gintsts = usb_rd(USB_GINTSTS);
    emu68_usbmouse_gintmsk = usb_rd(USB_GINTMSK);
    emu68_usbmouse_hnptxsts = usb_rd(USB_HNPTXSTS);
    emu68_usbmouse_hc0_hcchar = usb_rd(USB_HCCHAR0);
    emu68_usbmouse_hc0_hcsplt = usb_rd(USB_HCSPLT0);
    emu68_usbmouse_hc0_hcintmsk = usb_rd(USB_HCINTMSK0);
    emu68_usbmouse_hc0_hctsiz = usb_rd(USB_HCTSIZ0);
    emu68_usbmouse_hprt = usb_rd(USB_HPRT);
    emu68_usbmouse_port_connected =
        (emu68_usbmouse_hprt & USB_HPRT_PCSTS) ? 1U : 0U;
#endif
}

void emu68_usbmouse_diag_port_reset(uint8_t end_phase)
{
#if EMU68_USBMOUSE_DIAGNOSTICS
    if (end_phase) emu68_usbmouse_reset_ended = 1;
    else emu68_usbmouse_reset_started = 1;
#else
    (void)end_phase;
#endif
}

void emu68_usbmouse_diag_setup(uint8_t dev_addr, uint8_t const setup[8])
{
#if EMU68_USBMOUSE_DIAGNOSTICS
    emu68_usbmouse_setup_count++;
    emu68_usbmouse_setup_dev = dev_addr;
    emu68_usbmouse_setup_bmreq = setup[0];
    emu68_usbmouse_setup_breq = setup[1];
    emu68_usbmouse_setup_wvalue = (uint16_t)setup[2] | ((uint16_t)setup[3] << 8);
    emu68_usbmouse_setup_windex = (uint16_t)setup[4] | ((uint16_t)setup[5] << 8);
    emu68_usbmouse_setup_wlength = (uint16_t)setup[6] | ((uint16_t)setup[7] << 8);
#else
    (void)dev_addr;
    (void)setup;
#endif
}

void emu68_usbmouse_diag_hcint(uint8_t ch, uint32_t hcint, uint32_t hcchar, uint32_t hctsiz,
                                  uint32_t hcsplt, uint32_t hcintmsk,
                                  uint8_t root_speed, uint8_t dev_speed)
{
#if EMU68_USBMOUSE_DIAGNOSTICS
    emu68_usbmouse_hcint_count++;
    emu68_usbmouse_last_ch = ch;
    emu68_usbmouse_last_hcint = hcint;
    emu68_usbmouse_last_hcchar = hcchar;
    emu68_usbmouse_last_hctsiz = hctsiz;

    if (ch == 0 && emu68_usbmouse_hc_trace_count < EMU68_USBMOUSE_HC_TRACE_MAX) {
        uint32_t n = emu68_usbmouse_hc_trace_count++;
        emu68_usbmouse_hc_trace_int[n] = hcint;
        emu68_usbmouse_hc_trace_char[n] = hcchar;
        emu68_usbmouse_hc_trace_size[n] = hctsiz;
        emu68_usbmouse_hc_trace_split[n] = hcsplt;
        emu68_usbmouse_hc_trace_mask[n] = hcintmsk;
        emu68_usbmouse_hc_trace_root_speed[n] = root_speed;
        emu68_usbmouse_hc_trace_dev_speed[n] = dev_speed;
    }
#else
    (void)ch; (void)hcint; (void)hcchar; (void)hctsiz;
    (void)hcsplt; (void)hcintmsk; (void)root_speed; (void)dev_speed;
#endif
}
void emu68_usbmouse_diag_nptxfe(uint32_t gintsts, uint32_t gintmsk, uint32_t hnptxsts,
                                 uint32_t hcchar, uint32_t hctsiz, uint32_t hcintmsk)
{
#if EMU68_USBMOUSE_DIAGNOSTICS
    emu68_usbmouse_nptxfe_count++;
    emu68_usbmouse_nptxfe_gintsts = gintsts;
    emu68_usbmouse_nptxfe_gintmsk = gintmsk;
    emu68_usbmouse_hnptxsts = hnptxsts;
    emu68_usbmouse_hc0_hcchar = hcchar;
    emu68_usbmouse_hc0_hctsiz = hctsiz;
    emu68_usbmouse_hc0_hcintmsk = hcintmsk;
#else
    (void)gintsts; (void)gintmsk; (void)hnptxsts;
    (void)hcchar; (void)hctsiz; (void)hcintmsk;
#endif
}
void emu68_usbmouse_clear_motion(void)
{
    emu68_usbmouse_x = 0;
    emu68_usbmouse_y = 0;
    usbmouse_raw_x = 0;
    usbmouse_raw_y = 0;
    emu68_usbmouse_last_dx = 0;
    emu68_usbmouse_last_dy = 0;

    /* Restart the 50 Hz publication phase from "now". */
    asm volatile("mrs %0, CNTPCT_EL0" : "=r"(usbmouse_publish_last));
}

uint32_t tusb_time_millis_api(void)
{
    uint64_t now, freq;
    asm volatile("mrs %0, CNTPCT_EL0" : "=r"(now));
    asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
    return (uint32_t)((now * 1000ULL) / freq);
}

void emu68_usbmouse_init(void)
{
    uint32_t id;
    emu68_usbmouse_x = 0;
    emu68_usbmouse_y = 0;
    emu68_usbmouse_left = 0;
    emu68_usbmouse_right = 0;
    usbmouse_raw_x = 0;
    usbmouse_raw_y = 0;
    usbmouse_publish_last = 0;
    usbmouse_publish_ticks = 1;
    emu68_usbmouse_mounted = 0;
    emu68_usbmouse_host_init_ok = 0;
    emu68_usbmouse_host_mode = 0;
    emu68_usbmouse_port_connected = 0;
    emu68_usbmouse_enum_mounted = 0;
    emu68_usbmouse_enum_addr = 0;
    emu68_usbmouse_report_count = 0;
    emu68_usbmouse_last_dx = 0;
    emu68_usbmouse_last_dy = 0;
    emu68_usbmouse_gsnpsid = 0;
    emu68_usbmouse_reset_started = 0;
    emu68_usbmouse_reset_ended = 0;
    emu68_usbmouse_setup_count = 0;
    emu68_usbmouse_setup_dev = 0;
    emu68_usbmouse_setup_bmreq = 0;
    emu68_usbmouse_setup_breq = 0;
    emu68_usbmouse_setup_wvalue = 0;
    emu68_usbmouse_setup_windex = 0;
    emu68_usbmouse_setup_wlength = 0;
    emu68_usbmouse_hcint_count = 0;
    emu68_usbmouse_last_ch = 0;
    emu68_usbmouse_last_hcint = 0;
    emu68_usbmouse_last_hcchar = 0;
    emu68_usbmouse_last_hctsiz = 0;
    emu68_usbmouse_hprt = 0;
    emu68_usbmouse_gintsts = 0;

    /* 50 Hz publication timer for Amiga-visible movement. */
    {
        uint64_t freq;
        asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
        usbmouse_publish_ticks = (freq >= 50ULL) ? (freq / 50ULL) : 1ULL;
        asm volatile("mrs %0, CNTPCT_EL0" : "=r"(usbmouse_publish_last));
    }

    /* Same firmware power-domain operation proven by the UVC POC. */
    (void)set_power_state(3, 3);
    tusb_time_delay_ms_api(20);

    id = usb_rd(USB_GSNPSID);
    emu68_usbmouse_gsnpsid = id;
    kprintf("[USB-MOUSE] DWC2 GSNPSID=%08x\n", id);
    if ((id & 0xffff0000U) != 0x4f540000U) {
        kprintf("[USB-MOUSE] no Synopsys OTG core; host disabled\n");
        return;
    }

    const tusb_rhport_init_t init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO
    };

    if (!tusb_rhport_init(0, &init)) {
        kprintf("[USB-MOUSE] TinyUSB host init failed\n");
        return;
    }

    emu68_usbmouse_host_init_ok = 1;
    emu68_usbmouse_diag_sample();
    kprintf("[USB-MOUSE] TinyUSB DWC2 host ready; waiting for HID boot mouse\n");
}

/* Called frequently from the already-existing CPU2 PiStorm housekeeper.
 * Because platform IRQ enable is intentionally a no-op, first harvest the
 * DWC2 interrupt status synchronously, then drain TinyUSB's host event queue. */
void emu68_usbmouse_poll(void)
{
    uint64_t now;

    if (!tusb_inited()) return;

    /* Fast transport service: called at ~1 kHz by CPU2 at runtime. */
    tusb_int_handler(0, false);
    tuh_task_ext(0, false);

    /*
     * Publish movement only at 50 Hz.
     *
     * Sensitivity is 1:2.  Keep the signed remainder in usbmouse_raw_{x,y}
     * so two successive +/-1 HID counts become one Amiga count instead of
     * being rounded away.
     */
    asm volatile("mrs %0, CNTPCT_EL0" : "=r"(now));
    if ((now - usbmouse_publish_last) >= usbmouse_publish_ticks) {
        int32_t out_x = usbmouse_raw_x / 2;
        int32_t out_y = usbmouse_raw_y / 2;

        usbmouse_raw_x -= out_x * 2;
        usbmouse_raw_y -= out_y * 2;

        emu68_usbmouse_x = (uint8_t)(emu68_usbmouse_x + out_x);
        emu68_usbmouse_y = (uint8_t)(emu68_usbmouse_y + out_y);

        /*
         * Advance by one nominal period.  If servicing was delayed badly,
         * resynchronise rather than trying to "catch up" with bursts.
         */
        if ((now - usbmouse_publish_last) >= (usbmouse_publish_ticks * 2ULL))
            usbmouse_publish_last = now;
        else
            usbmouse_publish_last += usbmouse_publish_ticks;
    }

    emu68_usbmouse_diag_sample();
}

void tuh_mount_cb(uint8_t dev_addr)
{
    emu68_usbmouse_enum_mounted = 1;
    emu68_usbmouse_enum_addr = dev_addr;
    kprintf("[USB-MOUSE] USB device mounted addr=%u\n", (uint32_t)dev_addr);
}

void tuh_umount_cb(uint8_t dev_addr)
{
    if (emu68_usbmouse_enum_addr == dev_addr) {
        emu68_usbmouse_enum_mounted = 0;
        emu68_usbmouse_enum_addr = 0;
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len)
{
    (void)desc_report;
    (void)desc_len;

    uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    if (proto != HID_ITF_PROTOCOL_MOUSE) {
        kprintf("[USB-MOUSE] HID interface ignored (protocol=%u)\n", (uint32_t)proto);
        return;
    }

    emu68_usbmouse_mounted = 1;
    kprintf("[USB-MOUSE] HID boot mouse mounted addr=%u instance=%u\n",
            (uint32_t)dev_addr, (uint32_t)instance);

    if (!tuh_hid_receive_report(dev_addr, instance))
        kprintf("[USB-MOUSE] initial interrupt-IN request failed\n");
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    (void)dev_addr;
    (void)instance;
    emu68_usbmouse_mounted = 0;
    kprintf("[USB-MOUSE] mouse removed\n");
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len)
{
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_MOUSE &&
        len >= 3U) {
        /* HID boot mouse guarantees buttons, X, Y in the first three bytes.
         * Wheel/pan are intentionally ignored in this first POC. */
        int8_t dx = (int8_t)report[1];
        int8_t dy = (int8_t)report[2];

        /* HID Boot Mouse byte 0:
         * bit 0 = left button, bit 1 = right button.
         * Keep these full-rate; AmigaOS will observe them when it reads
         * CIAAPRA/POTGOR, exactly like the physical mouse lines. */
        emu68_usbmouse_left  = (report[0] & 0x01U) ? 1U : 0U;
        emu68_usbmouse_right = (report[0] & 0x02U) ? 1U : 0U;

        emu68_usbmouse_last_dx = dx;
        emu68_usbmouse_last_dy = dy;
        emu68_usbmouse_report_count++;

        /* Full-rate HID accumulation; publication to JOY0DAT is 50 Hz. */
        usbmouse_raw_x += (int32_t)dx;
        usbmouse_raw_y += (int32_t)dy;
    }

    if (!tuh_hid_receive_report(dev_addr, instance))
        kprintf("[USB-MOUSE] interrupt-IN rearm failed\n");
}

#else
volatile uint8_t emu68_usbmouse_x, emu68_usbmouse_y, emu68_usbmouse_mounted;
volatile uint8_t emu68_usbmouse_left, emu68_usbmouse_right;
volatile uint8_t emu68_usbmouse_host_init_ok, emu68_usbmouse_host_mode;
volatile uint8_t emu68_usbmouse_port_connected, emu68_usbmouse_enum_mounted;
volatile uint8_t emu68_usbmouse_enum_addr;
volatile uint32_t emu68_usbmouse_report_count, emu68_usbmouse_gsnpsid;
volatile int8_t emu68_usbmouse_last_dx, emu68_usbmouse_last_dy;
volatile uint8_t emu68_usbmouse_reset_started, emu68_usbmouse_reset_ended;
volatile uint32_t emu68_usbmouse_setup_count, emu68_usbmouse_hcint_count;
volatile uint8_t emu68_usbmouse_setup_dev, emu68_usbmouse_setup_bmreq, emu68_usbmouse_setup_breq, emu68_usbmouse_last_ch;
volatile uint16_t emu68_usbmouse_setup_wvalue, emu68_usbmouse_setup_windex, emu68_usbmouse_setup_wlength;
volatile uint32_t emu68_usbmouse_last_hcint, emu68_usbmouse_last_hcchar, emu68_usbmouse_last_hctsiz, emu68_usbmouse_hprt, emu68_usbmouse_gintsts;
void emu68_usbmouse_diag_port_reset(uint8_t x) {(void)x;}
void emu68_usbmouse_diag_setup(uint8_t a, uint8_t const s[8]) {(void)a;(void)s;}
void emu68_usbmouse_diag_hcint(uint8_t c,uint32_t i,uint32_t a,uint32_t z,uint32_t s,uint32_t m,uint8_t r,uint8_t d){(void)c;(void)i;(void)a;(void)z;(void)s;(void)m;(void)r;(void)d;}
void emu68_usbmouse_diag_nptxfe(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
void emu68_usbmouse_init(void) {}
void emu68_usbmouse_poll(void) {}
void emu68_usbmouse_clear_motion(void) {}
#endif
