/* generated vector source file - do not edit */
        #include "bsp_api.h"
        #include "ra_encoder.h"

/* Weak ISR stubs for CTSU. These allow builds to succeed even when the CTSU driver
 * (r_ctsu) is not compiled in. If r_ctsu is added, its strong ISR definitions will
 * override these weak symbols.
 */
BSP_WEAK_REFERENCE void ctsu_write_isr(void) { }
BSP_WEAK_REFERENCE void ctsu_read_isr(void) { }
BSP_WEAK_REFERENCE void ctsu_end_isr(void) { }
/* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
{
    [0] = rtc_alarm_periodic_isr,                     /* RTC ALARM (Alarm interrupt) */
    [1] = rtc_alarm_periodic_isr,         /* RTC PERIOD (Periodic interrupt) */
    [2] = rtc_carry_isr,         /* RTC CARRY (Carry interrupt) */
    [3] = spi_rxi_isr,         /* SPI0 RXI (Receive buffer full) */
    [4] = spi_txi_isr,         /* SPI0 TXI (Transmit buffer empty) */
    [5] = spi_tei_isr,         /* SPI0 TEI (Transmission complete event) */
    [6] = spi_eri_isr,         /* SPI0 ERI (Error) */
    [7] = sci_uart_rxi_isr,         /* SCI2 RXI (Received data full) */
    [8] = sci_uart_txi_isr,         /* SCI2 TXI (Transmit data empty) */
    [9] = sci_uart_tei_isr,         /* SCI2 TEI (Transmit end) */
    [10] = sci_uart_eri_isr,         /* SCI2 ERI (Receive error) */
    [11] = sci_uart_rxi_isr,         /* SCI9 RXI (Received data full) */
    [12] = sci_uart_txi_isr,         /* SCI9 TXI (Transmit data empty) */
    [13] = sci_uart_tei_isr,         /* SCI9 TEI (Transmit end) */
    [14] = sci_uart_eri_isr,         /* SCI9 ERI (Receive error) */
    [15] = r_icu_isr,         /* ICU IRQ0 (External pin interrupt 0) */
    [16] = usbfs_d0fifo_handler,         /* USBFS FIFO 0 (DMA transfer request 0) */
    [17] = usbfs_d1fifo_handler,         /* USBFS FIFO 1 (DMA transfer request 1) */
    [18] = usbfs_resume_handler,         /* USBFS RESUME (USBFS resume interrupt) */
    [19] = usbfs_interrupt_handler,         /* USBFS INT (USBFS interrupt) */
    [20] = ctsu_write_isr,         /* CTSU WRITE (CTSU write request interrupt) */
    [21] = ctsu_read_isr,         /* CTSU READ (CTSU measurement data transfer request interrupt) */
    [22] = ctsu_end_isr,         /* CTSU END (CTSU measurement end interrupt) */
    [23] = r_icu_isr,         /* ICU IRQ1 (External pin interrupt 1) */
    [24] = r_icu_isr,         /* ICU IRQ2 (External pin interrupt 2) */
    [25] = r_icu_isr,         /* ICU IRQ3 (External pin interrupt 3) */
    [26] = r_icu_isr,         /* ICU IRQ4 (External pin interrupt 4) */
    [27] = r_icu_isr,         /* ICU IRQ5 (External pin interrupt 5) */
    [28] = r_icu_isr,         /* ICU IRQ6 (External pin interrupt 6) */
    [29] = r_icu_isr,         /* ICU IRQ7 (External pin interrupt 7) */
    [30] = r_icu_isr,         /* ICU IRQ8 (External pin interrupt 8) */
    [31] = r_icu_isr,         /* ICU IRQ9 (External pin interrupt 9) */
    [32] = iic_master_rxi_isr,         /* IIC0 RXI (Receive data full) */
    [33] = iic_master_txi_isr,         /* IIC0 TXI (Transmit data empty) */
    [34] = iic_master_tei_isr,         /* IIC0 TEI (Transmit end) */
    [35] = iic_master_eri_isr,         /* IIC0 ERI (Transfer error) */
    [36] = iic_master_rxi_isr,         /* IIC1 RXI (Receive data full) */
    [37] = iic_master_txi_isr,         /* IIC1 TXI (Transmit data empty) */
    [38] = iic_master_tei_isr,         /* IIC1 TEI (Transmit end) */
    [39] = iic_master_eri_isr,         /* IIC1 ERI (Transfer error) */
    [40] = r_icu_isr,         /* ICU IRQ10 (External pin interrupt 10) */
    [41] = r_icu_isr,         /* ICU IRQ11 (External pin interrupt 11) */
    [42] = r_icu_isr,         /* ICU IRQ12 (External pin interrupt 12) */
    [43] = r_icu_isr,         /* ICU IRQ13 (External pin interrupt 13) */
    [44] = r_icu_isr,         /* ICU IRQ14 (External pin interrupt 14) */
    [45] = r_icu_isr,         /* ICU IRQ15 (External pin interrupt 15) */
    [46] = agt_int_isr,         /* AGT0 INT (AGT interrupt) */
    [47] = agt_int_isr,         /* AGT1 INT (AGT interrupt) */
    [48] = agt_int_isr,         /* AGT2 INT (AGT interrupt) */
    [49] = agt_int_isr,         /* AGT3 INT (AGT interrupt) */
    [50] = agt_int_isr,         /* AGT4 INT (AGT interrupt) */
    [51] = agt_int_isr,         /* AGT5 INT (AGT interrupt) */
    [52] = agt_int_isr,         /* AGT0 COMPAREA (Compare match A) */
    [53] = agt_int_isr,         /* AGT0 COMPAREB (Compare match B) */
    [54] = agt_int_isr,         /* AGT1 COMPAREA (Compare match A) */
    [55] = agt_int_isr,         /* AGT1 COMPAREB (Compare match B) */
    [56] = agt_int_isr,         /* AGT2 COMPAREA (Compare match A) */
    [57] = agt_int_isr,         /* AGT2 COMPAREB (Compare match B) */
    [58] = agt_int_isr,         /* AGT3 COMPAREA (Compare match A) */
    [59] = agt_int_isr,         /* AGT3 COMPAREB (Compare match B) */
    [60] = agt_int_isr,         /* AGT4 COMPAREA (Compare match A) */
    [61] = agt_int_isr,         /* AGT4 COMPAREB (Compare match B) */
    [62] = agt_int_isr,         /* AGT5 COMPAREA (Compare match A) */
    [63] = agt_int_isr,         /* AGT5 COMPAREB (Compare match B) */
    [64] = dmac_int_isr,         /* DMAC0 INT (DMAC transfer end 0) */
    [65] = dmac_int_isr,         /* DMAC1 INT (DMAC transfer end 1) */
    [66] = dmac_int_isr,         /* DMAC2 INT (DMAC transfer end 2) */
    [67] = dmac_int_isr,         /* DMAC3 INT (DMAC transfer end 3) */
    [68] = dmac_int_isr,         /* DMAC4 INT (DMAC transfer end 4) */
    [69] = dmac_int_isr,         /* DMAC5 INT (DMAC transfer end 5) */
    [70] = dmac_int_isr,         /* DMAC6 INT (DMAC transfer end 6) */
    [71] = dmac_int_isr,         /* DMAC7 INT (DMAC transfer end 7) */
    [72] = encoder_compare_a_isr,  /* GPT4 CAPTURE_COMPARE_A (Compare match A) */
    [73] = encoder_compare_b_isr,  /* GPT4 CAPTURE_COMPARE_B (Compare match B) */
    [74] = adc_scan_end_isr,       /* ADC0 SCAN END (A/D scan end interrupt) */
};
const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_MAX_ENTRIES] =
{
    [0] = BSP_PRV_IELS_ENUM(EVENT_RTC_ALARM),         /* RTC ALARM (Alarm interrupt) */
    [1] = BSP_PRV_IELS_ENUM(EVENT_RTC_PERIOD),         /* RTC PERIOD (Periodic interrupt) */
    [2] = BSP_PRV_IELS_ENUM(EVENT_RTC_CARRY),         /* RTC CARRY (Carry interrupt) */
    [3] = BSP_PRV_IELS_ENUM(EVENT_SPI0_RXI),         /* SPI0 RXI (Receive buffer full) */
    [4] = BSP_PRV_IELS_ENUM(EVENT_SPI0_TXI),         /* SPI0 TXI (Transmit buffer empty) */
    [5] = BSP_PRV_IELS_ENUM(EVENT_SPI0_TEI),         /* SPI0 TEI (Transmission complete event) */
    [6] = BSP_PRV_IELS_ENUM(EVENT_SPI0_ERI),         /* SPI0 ERI (Error) */
    [7] = BSP_PRV_IELS_ENUM(EVENT_SCI2_RXI),         /* SCI2 RXI (Received data full) */
    [8] = BSP_PRV_IELS_ENUM(EVENT_SCI2_TXI),         /* SCI2 TXI (Transmit data empty) */
    [9] = BSP_PRV_IELS_ENUM(EVENT_SCI2_TEI),         /* SCI2 TEI (Transmit end) */
    [10] = BSP_PRV_IELS_ENUM(EVENT_SCI2_ERI),         /* SCI2 ERI (Receive error) */
    [11] = BSP_PRV_IELS_ENUM(EVENT_SCI9_RXI),         /* SCI9 RXI (Received data full) */
    [12] = BSP_PRV_IELS_ENUM(EVENT_SCI9_TXI),         /* SCI9 TXI (Transmit data empty) */
    [13] = BSP_PRV_IELS_ENUM(EVENT_SCI9_TEI),         /* SCI9 TEI (Transmit end) */
    [14] = BSP_PRV_IELS_ENUM(EVENT_SCI9_ERI),         /* SCI9 ERI (Receive error) */
    [15] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ0),         /* ICU IRQ0 (External pin interrupt 0) */
    [16] = BSP_PRV_IELS_ENUM(EVENT_USBFS_FIFO_0),         /* USBFS FIFO 0 (DMA transfer request 0) */
    [17] = BSP_PRV_IELS_ENUM(EVENT_USBFS_FIFO_1),         /* USBFS FIFO 1 (DMA transfer request 1) */
    [18] = BSP_PRV_IELS_ENUM(EVENT_USBFS_RESUME),         /* USBFS RESUME (USBFS resume interrupt) */
    [19] = BSP_PRV_IELS_ENUM(EVENT_USBFS_INT),         /* USBFS INT (USBFS interrupt) */
    [20] = BSP_PRV_IELS_ENUM(EVENT_CTSU_WRITE),         /* CTSU WRITE (CTSU write request interrupt) */
    [21] = BSP_PRV_IELS_ENUM(EVENT_CTSU_READ),         /* CTSU READ (CTSU measurement data transfer request interrupt) */
    [22] = BSP_PRV_IELS_ENUM(EVENT_CTSU_END),         /* CTSU END (CTSU measurement end interrupt) */
    [23] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ1),         /* ICU IRQ1 (External pin interrupt 1) */
    [24] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ2),         /* ICU IRQ2 (External pin interrupt 2) */
    [25] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ3),         /* ICU IRQ3 (External pin interrupt 3) */
    [26] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ4),         /* ICU IRQ4 (External pin interrupt 4) */
    [27] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ5),         /* ICU IRQ5 (External pin interrupt 5) */
    [28] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ6),         /* ICU IRQ6 (External pin interrupt 6) */
    [29] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ7),         /* ICU IRQ7 (External pin interrupt 7) */
    [30] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ8),         /* ICU IRQ8 (External pin interrupt 8) */
    [31] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ9),         /* ICU IRQ9 (External pin interrupt 9) */
    [32] = BSP_PRV_IELS_ENUM(EVENT_IIC0_RXI),         /* IIC0 RXI (Receive data full) */
    [33] = BSP_PRV_IELS_ENUM(EVENT_IIC0_TXI),         /* IIC0 TXI (Transmit data empty) */
    [34] = BSP_PRV_IELS_ENUM(EVENT_IIC0_TEI),         /* IIC0 TEI (Transmit end) */
    [35] = BSP_PRV_IELS_ENUM(EVENT_IIC0_ERI),         /* IIC0 ERI (Transfer error) */
    [36] = BSP_PRV_IELS_ENUM(EVENT_IIC1_RXI),         /* IIC1 RXI (Receive data full) */
    [37] = BSP_PRV_IELS_ENUM(EVENT_IIC1_TXI),         /* IIC1 TXI (Transmit data empty) */
    [38] = BSP_PRV_IELS_ENUM(EVENT_IIC1_TEI),         /* IIC1 TEI (Transmit end) */
    [39] = BSP_PRV_IELS_ENUM(EVENT_IIC1_ERI),         /* IIC1 ERI (Transfer error) */
    [40] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ10),         /* ICU IRQ10 (External pin interrupt 10) */
    [41] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ11),         /* ICU IRQ11 (External pin interrupt 11) */
    [42] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ12),         /* ICU IRQ12 (External pin interrupt 12) */
    [43] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ13),         /* ICU IRQ13 (External pin interrupt 13) */
    [44] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ14),         /* ICU IRQ14 (External pin interrupt 14) */
    [45] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ15),         /* ICU IRQ15 (External pin interrupt 15) */
    [46] = BSP_PRV_IELS_ENUM(EVENT_AGT0_INT),         /* AGT0 INT (AGT interrupt) */
    [47] = BSP_PRV_IELS_ENUM(EVENT_AGT1_INT),         /* AGT1 INT (AGT interrupt) */
    [48] = BSP_PRV_IELS_ENUM(EVENT_AGT2_INT),         /* AGT2 INT (AGT interrupt) */
    [49] = BSP_PRV_IELS_ENUM(EVENT_AGT3_INT),         /* AGT3 INT (AGT interrupt) */
    [50] = BSP_PRV_IELS_ENUM(EVENT_AGT4_INT),         /* AGT4 INT (AGT interrupt) */
    [51] = BSP_PRV_IELS_ENUM(EVENT_AGT5_INT),         /* AGT5 INT (AGT interrupt) */
    [52] = BSP_PRV_IELS_ENUM(EVENT_AGT0_COMPARE_A),         /* AGT0 COMPAREA (Compare match A) */
    [53] = BSP_PRV_IELS_ENUM(EVENT_AGT0_COMPARE_B),         /* AGT0 COMPAREB (Compare match B) */
    [54] = BSP_PRV_IELS_ENUM(EVENT_AGT1_COMPARE_A),         /* AGT1 COMPAREA (Compare match A) */
    [55] = BSP_PRV_IELS_ENUM(EVENT_AGT1_COMPARE_B),         /* AGT1 COMPAREB (Compare match B) */
    [56] = BSP_PRV_IELS_ENUM(EVENT_AGT2_COMPARE_A),         /* AGT2 COMPAREA (Compare match A) */
    [57] = BSP_PRV_IELS_ENUM(EVENT_AGT2_COMPARE_B),         /* AGT2 COMPAREB (Compare match B) */
    [58] = BSP_PRV_IELS_ENUM(EVENT_AGT3_COMPARE_A),         /* AGT3 COMPAREA (Compare match A) */
    [59] = BSP_PRV_IELS_ENUM(EVENT_AGT3_COMPARE_B),         /* AGT3 COMPAREB (Compare match B) */
    [60] = BSP_PRV_IELS_ENUM(EVENT_AGT4_COMPARE_A),         /* AGT4 COMPAREA (Compare match A) */
    [61] = BSP_PRV_IELS_ENUM(EVENT_AGT4_COMPARE_B),         /* AGT4 COMPAREB (Compare match B) */
    [62] = BSP_PRV_IELS_ENUM(EVENT_AGT5_COMPARE_A),         /* AGT5 COMPAREA (Compare match A) */
    [63] = BSP_PRV_IELS_ENUM(EVENT_AGT5_COMPARE_B),         /* AGT5 COMPAREB (Compare match B) */
    [64] = BSP_PRV_IELS_ENUM(EVENT_DMAC0_INT),         /* DMAC0 INT (DMAC transfer end 0) */
    [65] = BSP_PRV_IELS_ENUM(EVENT_DMAC1_INT),         /* DMAC1 INT (DMAC transfer end 1) */
    [66] = BSP_PRV_IELS_ENUM(EVENT_DMAC2_INT),         /* DMAC2 INT (DMAC transfer end 2) */
    [67] = BSP_PRV_IELS_ENUM(EVENT_DMAC3_INT),         /* DMAC3 INT (DMAC transfer end 3) */
    [68] = BSP_PRV_IELS_ENUM(EVENT_DMAC4_INT),         /* DMAC4 INT (DMAC transfer end 4) */
    [69] = BSP_PRV_IELS_ENUM(EVENT_DMAC5_INT),         /* DMAC5 INT (DMAC transfer end 5) */
    [70] = BSP_PRV_IELS_ENUM(EVENT_DMAC6_INT),         /* DMAC6 INT (DMAC transfer end 6) */
    [71] = BSP_PRV_IELS_ENUM(EVENT_DMAC7_INT),         /* DMAC7 INT (DMAC transfer end 7) */
    [72] = BSP_PRV_IELS_ENUM(EVENT_GPT4_CAPTURE_COMPARE_A),  /* GPT4 CAPTURE_COMPARE_A (Compare match A) */
    [73] = BSP_PRV_IELS_ENUM(EVENT_GPT4_CAPTURE_COMPARE_B),  /* GPT4 CAPTURE_COMPARE_B (Compare match B) */
    [74] = BSP_PRV_IELS_ENUM(EVENT_ADC0_SCAN_END),           /* ADC0 SCAN END (A/D scan end interrupt) */
};
        #endif
