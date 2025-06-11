/* generated vector source file - do not edit */
#include "bsp_api.h"
/* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
#if VECTOR_DATA_IRQ_COUNT > 0
BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
{
    [0] = sci_uart_rxi_isr,         /* SCI6 RXI (Received data full) */
    [1] = sci_uart_txi_isr,         /* SCI6 TXI (Transmit data empty) */
    [2] = sci_uart_tei_isr,         /* SCI6 TEI (Transmit end) */
    [3] = sci_uart_eri_isr,         /* SCI6 ERI (Receive error) */
    [4] = sci_uart_rxi_isr,         /* SCI7 RXI (Received data full) */
    [5] = sci_uart_txi_isr,         /* SCI7 TXI (Transmit data empty) */
    [6] = sci_uart_tei_isr,         /* SCI7 TEI (Transmit end) */
    [7] = sci_uart_eri_isr,         /* SCI7 ERI (Receive error) */
    [8] = sci_uart_rxi_isr,         /* SCI9 RXI (Received data full) */
    [9] = sci_uart_txi_isr,         /* SCI9 TXI (Transmit data empty) */
    [10] = sci_uart_tei_isr,         /* SCI9 TEI (Transmit end) */
    [11] = sci_uart_eri_isr,         /* SCI9 ERI (Receive error) */
    [12] = rtc_alarm_periodic_isr,         /* RTC ALARM (Alarm interrupt) */
    [13] = rtc_alarm_periodic_isr,         /* RTC PERIOD (Periodic interrupt) */
    [14] = rtc_carry_isr,         /* RTC CARRY (Carry interrupt) */
    [15] = agt_int_isr,         /* AGT0 INT (AGT interrupt) */
    [16] = agt_int_isr,         /* AGT1 INT (AGT interrupt) */
    [17] = r_icu_isr,         /* ICU IRQ7 (External pin interrupt 7) */
    [18] = r_icu_isr,         /* ICU IRQ11 (External pin interrupt 11) */
    [19] = r_icu_isr,         /* ICU IRQ13 (External pin interrupt 13) */
    [20] = r_icu_isr,         /* ICU IRQ14 (External pin interrupt 14) */
    [21] = r_icu_isr,         /* ICU IRQ  (External pin interrupt 5) */
    [22] = r_icu_isr,         /* ICU IRQ  (External pin interrupt 9) */
    [23] = r_icu_isr,         /* ICU IRQ   (External pin interrupt 10) */
    [24] = r_icu_isr,         /* ICU IRQ  (External pin interrupt 12) */
    [25] = spi_rxi_isr,         /* SPI0 RXI (Receive buffer full) */
    [26] = spi_txi_isr,         /* SPI0 TXI (Transmit buffer empty) */
    [27] = spi_tei_isr,         /* SPI0 TEI (Transmission complete event) */
    [28] = spi_eri_isr,         /* SPI0 ERI (Error) */
    [29] = iic_master_rxi_isr,         /* IIC2 RXI (Receive data full) */
    [30] = iic_master_txi_isr,         /* IIC2 TXI (Transmit data empty) */
    [31] = iic_master_tei_isr,         /* IIC2 TEI (Transmit end) */
    [32] = iic_master_eri_isr,         /* IIC2 ERI (Transfer error) */
    [33] = sdhimmc_accs_isr,         /* SDHIMMC0 ACCS (Card access) */
    [34] = sdhimmc_card_isr,         /* SDHIMMC0 CARD (Card detect) */
    [35] = sdhimmc_dma_req_isr,         /* SDHIMMC0 DMA REQ (DMA transfer request) */
    [36] = ether_eint_isr,         /* EDMAC0 EINT (EDMAC 0 interrupt) */
    [37] = usbfs_interrupt_handler, /* USBFS INT (USBFS interrupt) */
    [38] = usbfs_resume_handler, /* USBFS RESUME (USBFS resume interrupt) */
    [39] = usbfs_d0fifo_handler, /* USBFS FIFO 0 (DMA transfer request 0) */
    [40] = usbfs_d1fifo_handler, /* USBFS FIFO 1 (DMA transfer request 1) */
    [41] = usbhs_interrupt_handler, /* USBHS USB INT RESUME (USBHS interrupt) */
    [42] = usbhs_d0fifo_handler, /* USBHS FIFO 0 (DMA transfer request 0) */
    [43] = usbhs_d1fifo_handler, /* USBHS FIFO 1 (DMA transfer request 1) */

};
const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_MAX_ENTRIES] =
{
    [0] = BSP_PRV_IELS_ENUM(EVENT_SCI6_RXI),         /* SCI6 RXI (Received data full) */
    [1] = BSP_PRV_IELS_ENUM(EVENT_SCI6_TXI),         /* SCI6 TXI (Transmit data empty) */
    [2] = BSP_PRV_IELS_ENUM(EVENT_SCI6_TEI),         /* SCI6 TEI (Transmit end) */
    [3] = BSP_PRV_IELS_ENUM(EVENT_SCI6_ERI),         /* SCI6 ERI (Receive error) */
    [4] = BSP_PRV_IELS_ENUM(EVENT_SCI7_RXI),         /* SCI7 RXI (Received data full) */
    [5] = BSP_PRV_IELS_ENUM(EVENT_SCI7_TXI),         /* SCI7 TXI (Transmit data empty) */
    [6] = BSP_PRV_IELS_ENUM(EVENT_SCI7_TEI),         /* SCI7 TEI (Transmit end) */
    [7] = BSP_PRV_IELS_ENUM(EVENT_SCI7_ERI),         /* SCI7 ERI (Receive error) */
    [8] = BSP_PRV_IELS_ENUM(EVENT_SCI9_RXI),         /* SCI9 RXI (Received data full) */
    [9] = BSP_PRV_IELS_ENUM(EVENT_SCI9_TXI),         /* SCI9 TXI (Transmit data empty) */
    [10] = BSP_PRV_IELS_ENUM(EVENT_SCI9_TEI),         /* SCI9 TEI (Transmit end) */
    [11] = BSP_PRV_IELS_ENUM(EVENT_SCI9_ERI),         /* SCI9 ERI (Receive error) */
    [12] = BSP_PRV_IELS_ENUM(EVENT_RTC_ALARM),         /* RTC ALARM (Alarm interrupt) */
    [13] = BSP_PRV_IELS_ENUM(EVENT_RTC_PERIOD),         /* RTC PERIOD (Periodic interrupt) */
    [14] = BSP_PRV_IELS_ENUM(EVENT_RTC_CARRY),         /* RTC CARRY (Carry interrupt) */
    [15] = BSP_PRV_IELS_ENUM(EVENT_AGT0_INT),         /* AGT0 INT (AGT interrupt) */
    [16] = BSP_PRV_IELS_ENUM(EVENT_AGT1_INT),         /* AGT1 INT (AGT interrupt) */
    [17] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ7),         /* ICU IRQ7 (External pin interrupt 7) */
    [18] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ11),         /* ICU IRQ11 (External pin interrupt 11) */
    [19] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ13),         /* ICU IRQ13 (External pin interrupt 13) */
    [20] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ14),         /* ICU IRQ14 (External pin interrupt 14) */
    [21] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ5),         /* ICU IRQ5 (External pin interrupt 7) */
    [22] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ9),         /* ICU IRQ9 (External pin interrupt 11) */
    [23] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ10),         /* ICU IRQ10 (External pin interrupt 13) */
    [24] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ12),         /* ICU IRQ12 (External pin interrupt 14) */
    [25] = BSP_PRV_IELS_ENUM(EVENT_SPI0_RXI),         /* SPI0 RXI (Receive buffer full) */
    [26] = BSP_PRV_IELS_ENUM(EVENT_SPI0_TXI),         /* SPI0 TXI (Transmit buffer empty) */
    [27] = BSP_PRV_IELS_ENUM(EVENT_SPI0_TEI),         /* SPI0 TEI (Transmission complete event) */
    [28] = BSP_PRV_IELS_ENUM(EVENT_SPI0_ERI),         /* SPI0 ERI (Error) */
    [29] = BSP_PRV_IELS_ENUM(EVENT_IIC2_RXI),         /* IIC2 RXI (Receive data full) */
    [30] = BSP_PRV_IELS_ENUM(EVENT_IIC2_TXI),         /* IIC2 TXI (Transmit data empty) */
    [31] = BSP_PRV_IELS_ENUM(EVENT_IIC2_TEI),         /* IIC2 TEI (Transmit end) */
    [32] = BSP_PRV_IELS_ENUM(EVENT_IIC2_ERI),         /* IIC2 ERI (Transfer error) */
    [33] = BSP_PRV_IELS_ENUM(EVENT_SDHIMMC0_ACCS),         /* SDHIMMC0 ACCS (Card access) */
    [34] = BSP_PRV_IELS_ENUM(EVENT_SDHIMMC0_CARD),         /* SDHIMMC0 CARD (Card detect) */
    [35] = BSP_PRV_IELS_ENUM(EVENT_SDHIMMC0_DMA_REQ),         /* SDHIMMC0 DMA REQ (DMA transfer request) */
    [36] = BSP_PRV_IELS_ENUM(EVENT_EDMAC0_EINT),         /* EDMAC0 EINT (EDMAC 0 interrupt) */
    [37] = BSP_PRV_IELS_ENUM(EVENT_USBFS_INT), /* USBFS INT (USBFS interrupt) */
    [38] = BSP_PRV_IELS_ENUM(EVENT_USBFS_RESUME), /* USBFS RESUME (USBFS resume interrupt) */
    [39] = BSP_PRV_IELS_ENUM(EVENT_USBFS_FIFO_0), /* USBFS FIFO 0 (DMA transfer request 0) */
    [40] = BSP_PRV_IELS_ENUM(EVENT_USBFS_FIFO_1), /* USBFS FIFO 1 (DMA transfer request 1) */
    [41] = BSP_PRV_IELS_ENUM(EVENT_USBHS_USB_INT_RESUME), /* USBHS USB INT RESUME (USBHS interrupt) */
    [42] = BSP_PRV_IELS_ENUM(EVENT_USBHS_FIFO_0), /* USBHS FIFO 0 (DMA transfer request 0) */
    [43] = BSP_PRV_IELS_ENUM(EVENT_USBHS_FIFO_1), /* USBHS FIFO 1 (DMA transfer request 1) */
};
#endif