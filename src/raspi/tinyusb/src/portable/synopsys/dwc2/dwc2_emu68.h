/* Minimal TinyUSB DWC2 port glue for Emu68 / BCM2837 (Pi 3A+). */
#ifndef TUSB_DWC2_EMU68_H_
#define TUSB_DWC2_EMU68_H_

#define DWC2_EP_MAX 8

static const dwc2_controller_t _dwc2_controller[] = {
    { .reg_base = 0xf2980000UL, .irqnum = 0, .ep_count = DWC2_EP_MAX,
      .ep_in_count = DWC2_EP_MAX, .otg_dfifo_depth = 4096 }
};

/* Emu68 POC deliberately polls the controller from CPU2's existing
 * PiStorm housekeeper. TinyUSB's platform IRQ gate is therefore a no-op. */
TU_ATTR_ALWAYS_INLINE static inline void dwc2_int_set(uint8_t rhport, tusb_role_t role, bool enabled)
{ (void)rhport; (void)role; (void)enabled; }
#define dwc2_dcd_int_enable(_rhport)  dwc2_int_set((_rhport), TUSB_ROLE_DEVICE, true)
#define dwc2_dcd_int_disable(_rhport) dwc2_int_set((_rhport), TUSB_ROLE_DEVICE, false)

TU_ATTR_ALWAYS_INLINE static inline void dwc2_clock_init(uint8_t rhport, tusb_role_t role)
{ (void)rhport; (void)role; }
static inline void dwc2_phy_init(dwc2_regs_t *dwc2, uint8_t hs_phy_type)
{ (void)dwc2; (void)hs_phy_type; }
static inline void dwc2_phy_update(dwc2_regs_t *dwc2, uint8_t hs_phy_type)
{ (void)dwc2; (void)hs_phy_type; }
static inline void dwc2_phy_deinit(dwc2_regs_t *dwc2, uint8_t hs_phy_type)
{ (void)dwc2; (void)hs_phy_type; }
static inline void dwc2_remote_wakeup_delay(void) {}

#endif
