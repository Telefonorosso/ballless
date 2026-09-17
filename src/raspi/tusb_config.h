/* Emu68 PiStorm Classic USB mouse POC: TinyUSB host-only configuration. */
#ifndef EMU68_TUSB_CONFIG_H_
#define EMU68_TUSB_CONFIG_H_

#define CFG_TUSB_MCU                 OPT_MCU_BCM2837
#define CFG_TUSB_OS                  OPT_OS_NONE
#define CFG_TUSB_DEBUG               0
#define CFG_TUSB_RHPORT0_MODE        (OPT_MODE_HOST | OPT_MODE_HIGH_SPEED)
#define CFG_TUH_ENABLED              1
#define CFG_TUD_ENABLED              0
#define CFG_TUH_MAX_SPEED            OPT_MODE_HIGH_SPEED
#define CFG_TUH_DEVICE_MAX           1
#define CFG_TUH_ENUMERATION_BUFSIZE  256
#define CFG_TUH_HUB                  0
#define CFG_TUH_CDC                  0
#define CFG_TUH_HID                  1
#define CFG_TUH_MSC                  0
#define CFG_TUH_VENDOR               0
#define CFG_TUH_HID_EP_BUFSIZE       64
#define CFG_TUH_HID_SET_PROTOCOL_ON_ENUM 1
#define CFG_TUH_DWC2_SLAVE_ENABLE    1
#define CFG_TUH_DWC2_DMA_ENABLE      0
#define CFG_TUH_DWC2_ENDPOINT_MAX    8
#define CFG_TUH_MEM_ALIGN            __attribute__((aligned(4)))
#define CFG_TUH_MEM_SECTION

#endif
