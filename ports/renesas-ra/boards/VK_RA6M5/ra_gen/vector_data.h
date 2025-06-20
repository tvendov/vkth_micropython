/* generated vector header file - do not edit */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H
#ifdef __cplusplus
extern "C" {
#endif
/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
#define VECTOR_DATA_IRQ_COUNT    (44)
#endif
/* ISR prototypes */
void sci_uart_rxi_isr(void);
void sci_uart_txi_isr(void);
void sci_uart_tei_isr(void);
void sci_uart_eri_isr(void);
void rtc_alarm_periodic_isr(void);
void rtc_carry_isr(void);
void agt_int_isr(void);
void r_icu_isr(void);
void spi_rxi_isr(void);
void spi_txi_isr(void);
void spi_tei_isr(void);
void spi_eri_isr(void);
void iic_master_rxi_isr(void);
void iic_master_txi_isr(void);
void iic_master_tei_isr(void);
void iic_master_eri_isr(void);
void sdhimmc_accs_isr(void);
void sdhimmc_card_isr(void);
void sdhimmc_dma_req_isr(void);
void ether_eint_isr(void);
void usbfs_interrupt_handler(void);
void usbfs_resume_handler(void);
void usbfs_d0fifo_handler(void);
void usbfs_d1fifo_handler(void);
void usbhs_interrupt_handler(void);
void usbhs_d0fifo_handler(void);
void usbhs_d1fifo_handler(void);

/* Vector table allocations */
#define VECTOR_NUMBER_SCI6_RXI ((IRQn_Type)0)  /* SCI6 RXI (Received data full) */
#define SCI6_RXI_IRQn          ((IRQn_Type)0)  /* SCI6 RXI (Received data full) */
#define VECTOR_NUMBER_SCI6_TXI ((IRQn_Type)1)  /* SCI6 TXI (Transmit data empty) */
#define SCI6_TXI_IRQn          ((IRQn_Type)1)  /* SCI6 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI6_TEI ((IRQn_Type)2)  /* SCI6 TEI (Transmit end) */
#define SCI6_TEI_IRQn          ((IRQn_Type)2)  /* SCI6 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI6_ERI ((IRQn_Type)3)  /* SCI6 ERI (Receive error) */
#define SCI6_ERI_IRQn          ((IRQn_Type)3)  /* SCI6 ERI (Receive error) */
#define VECTOR_NUMBER_SCI7_RXI ((IRQn_Type)4)  /* SCI7 RXI (Received data full) */
#define SCI7_RXI_IRQn          ((IRQn_Type)4)  /* SCI7 RXI (Received data full) */
#define VECTOR_NUMBER_SCI7_TXI ((IRQn_Type)5)  /* SCI7 TXI (Transmit data empty) */
#define SCI7_TXI_IRQn          ((IRQn_Type)5)  /* SCI7 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI7_TEI ((IRQn_Type)6)  /* SCI7 TEI (Transmit end) */
#define SCI7_TEI_IRQn          ((IRQn_Type)6)  /* SCI7 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI7_ERI ((IRQn_Type)7)  /* SCI7 ERI (Receive error) */
#define SCI7_ERI_IRQn          ((IRQn_Type)7)  /* SCI7 ERI (Receive error) */
#define VECTOR_NUMBER_SCI9_RXI ((IRQn_Type)8)  /* SCI9 RXI (Received data full) */
#define SCI9_RXI_IRQn          ((IRQn_Type)8)  /* SCI9 RXI (Received data full) */
#define VECTOR_NUMBER_SCI9_TXI ((IRQn_Type)9)  /* SCI9 TXI (Transmit data empty) */
#define SCI9_TXI_IRQn          ((IRQn_Type)9)  /* SCI9 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI9_TEI ((IRQn_Type)10)  /* SCI9 TEI (Transmit end) */
#define SCI9_TEI_IRQn          ((IRQn_Type)10)  /* SCI9 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI9_ERI ((IRQn_Type)11)  /* SCI9 ERI (Receive error) */
#define SCI9_ERI_IRQn          ((IRQn_Type)11)  /* SCI9 ERI (Receive error) */
#define VECTOR_NUMBER_RTC_ALARM ((IRQn_Type)12)  /* RTC ALARM (Alarm interrupt) */
#define RTC_ALARM_IRQn          ((IRQn_Type)12)  /* RTC ALARM (Alarm interrupt) */
#define VECTOR_NUMBER_RTC_PERIOD ((IRQn_Type)13)  /* RTC PERIOD (Periodic interrupt) */
#define RTC_PERIOD_IRQn          ((IRQn_Type)13)  /* RTC PERIOD (Periodic interrupt) */
#define VECTOR_NUMBER_RTC_CARRY ((IRQn_Type)14)  /* RTC CARRY (Carry interrupt) */
#define RTC_CARRY_IRQn          ((IRQn_Type)14)  /* RTC CARRY (Carry interrupt) */
#define VECTOR_NUMBER_AGT0_INT ((IRQn_Type)15)  /* AGT0 INT (AGT interrupt) */
#define AGT0_INT_IRQn          ((IRQn_Type)15)  /* AGT0 INT (AGT interrupt) */
#define VECTOR_NUMBER_AGT1_INT ((IRQn_Type)16)  /* AGT1 INT (AGT interrupt) */
#define AGT1_INT_IRQn          ((IRQn_Type)16)  /* AGT1 INT (AGT interrupt) */
#define VECTOR_NUMBER_ICU_IRQ7 ((IRQn_Type)17)  /* ICU IRQ7 (External pin interrupt 7) */
#define ICU_IRQ7_IRQn          ((IRQn_Type)17)  /* ICU IRQ7 (External pin interrupt 7) */
#define VECTOR_NUMBER_ICU_IRQ11 ((IRQn_Type)18)  /* ICU IRQ11 (External pin interrupt 11) */
#define ICU_IRQ11_IRQn          ((IRQn_Type)18)  /* ICU IRQ11 (External pin interrupt 11) */
#define VECTOR_NUMBER_ICU_IRQ13 ((IRQn_Type)19)  /* ICU IRQ13 (External pin interrupt 13) */
#define ICU_IRQ13_IRQn          ((IRQn_Type)19)  /* ICU IRQ13 (External pin interrupt 13) */
#define VECTOR_NUMBER_ICU_IRQ14 ((IRQn_Type)20)  /* ICU IRQ14 (External pin interrupt 14) */
#define ICU_IRQ14_IRQn          ((IRQn_Type)20)  /* ICU IRQ14 (External pin interrupt 14) */
#define VECTOR_NUMBER_ICU_IRQ5  ((IRQn_Type)21)
#define ICU_IRQ5_IRQn           ((IRQn_Type)21)
#define VECTOR_NUMBER_ICU_IRQ9  ((IRQn_Type)22)
#define ICU_IRQ9_IRQn           ((IRQn_Type)22)
#define VECTOR_NUMBER_ICU_IRQ10 ((IRQn_Type)23)
#define ICU_IRQ10_IRQn           ((IRQn_Type)23)
#define VECTOR_NUMBER_ICU_IRQ12  ((IRQn_Type)24)
#define ICU_IRQ12_IRQn           ((IRQn_Type)24)

#define VECTOR_NUMBER_SPI0_RXI ((IRQn_Type)25) /* SPI0 RXI (Receive buffer full) */
#define SPI0_RXI_IRQn          ((IRQn_Type)25) /* SPI0 RXI (Receive buffer full) */
#define VECTOR_NUMBER_SPI0_TXI ((IRQn_Type)26) /* SPI0 TXI (Transmit buffer empty) */
#define SPI0_TXI_IRQn          ((IRQn_Type)26)  /* SPI0 TXI (Transmit buffer empty) */
#define VECTOR_NUMBER_SPI0_TEI ((IRQn_Type)27)  /* SPI0 TEI (Transmission complete event) */
#define SPI0_TEI_IRQn          ((IRQn_Type)27)  /* SPI0 TEI (Transmission complete event) */
#define VECTOR_NUMBER_SPI0_ERI ((IRQn_Type)28)  /* SPI0 ERI (Error) */
#define SPI0_ERI_IRQn          ((IRQn_Type)28)  /* SPI0 ERI (Error) */
#define VECTOR_NUMBER_IIC2_RXI ((IRQn_Type)29)  /* IIC2 RXI (Receive data full) */
#define IIC2_RXI_IRQn          ((IRQn_Type)29)  /* IIC2 RXI (Receive data full) */
#define VECTOR_NUMBER_IIC2_TXI ((IRQn_Type)30)  /* IIC2 TXI (Transmit data empty) */
#define IIC2_TXI_IRQn          ((IRQn_Type)30)  /* IIC2 TXI (Transmit data empty) */
#define VECTOR_NUMBER_IIC2_TEI ((IRQn_Type)31)  /* IIC2 TEI (Transmit end) */
#define IIC2_TEI_IRQn          ((IRQn_Type)31)  /* IIC2 TEI (Transmit end) */
#define VECTOR_NUMBER_IIC2_ERI ((IRQn_Type)32)  /* IIC2 ERI (Transfer error) */
#define IIC2_ERI_IRQn          ((IRQn_Type)32)  /* IIC2 ERI (Transfer error) */
#define VECTOR_NUMBER_SDHIMMC0_ACCS ((IRQn_Type)33)  /* SDHIMMC0 ACCS (Card access) */
#define SDHIMMC0_ACCS_IRQn          ((IRQn_Type)33)  /* SDHIMMC0 ACCS (Card access) */
#define VECTOR_NUMBER_SDHIMMC0_CARD ((IRQn_Type)34)  /* SDHIMMC0 CARD (Card detect) */
#define SDHIMMC0_CARD_IRQn          ((IRQn_Type)34)  /* SDHIMMC0 CARD (Card detect) */
#define VECTOR_NUMBER_SDHIMMC0_DMA_REQ ((IRQn_Type)35)  /* SDHIMMC0 DMA REQ (DMA transfer request) */
#define SDHIMMC0_DMA_REQ_IRQn          ((IRQn_Type)35)  /* SDHIMMC0 DMA REQ (DMA transfer request) */
#define VECTOR_NUMBER_EDMAC0_EINT ((IRQn_Type)36)  /* EDMAC0 EINT (EDMAC 0 interrupt) */
#define EDMAC0_EINT_IRQn          ((IRQn_Type)36)  /* EDMAC0 EINT (EDMAC 0 interrupt) */
#define VECTOR_NUMBER_USBFS_INT ((IRQn_Type)37)  /* USBFS INT (USBFS interrupt) */
#define USBFS_INT_IRQn          ((IRQn_Type)37)  /* USBFS INT (USBFS interrupt) */
#define VECTOR_NUMBER_USBFS_RESUME ((IRQn_Type)38)  /* USBFS RESUME (USBFS resume interrupt) */
#define USBFS_RESUME_IRQn          ((IRQn_Type)38)  /* USBFS RESUME (USBFS resume interrupt) */
#define VECTOR_NUMBER_USBFS_FIFO_0 ((IRQn_Type)39)  /* USBFS FIFO 0 (DMA transfer request 0) */
#define USBFS_FIFO_0_IRQn          ((IRQn_Type)39)  /* USBFS FIFO 0 (DMA transfer request 0) */
#define VECTOR_NUMBER_USBFS_FIFO_1 ((IRQn_Type)40)  /* USBFS FIFO 1 (DMA transfer request 1) */
#define USBFS_FIFO_1_IRQn          ((IRQn_Type)40)  /* USBFS FIFO 1 (DMA transfer request 1) */
#define VECTOR_NUMBER_USBHS_USB_INT_RESUME ((IRQn_Type)41)  /* USBHS USB INT RESUME (USBHS interrupt) */
#define USBHS_USB_INT_RESUME_IRQn          ((IRQn_Type)41)  /* USBHS USB INT RESUME (USBHS interrupt) */
#define VECTOR_NUMBER_USBHS_FIFO_0 ((IRQn_Type)42)  /* USBHS FIFO 0 (DMA transfer request 0) */
#define USBHS_FIFO_0_IRQn          ((IRQn_Type)42)  /* USBHS FIFO 0 (DMA transfer request 0) */
#define VECTOR_NUMBER_USBHS_FIFO_1 ((IRQn_Type)43)  /* USBHS FIFO 1 (DMA transfer request 1) */
#define USBHS_FIFO_1_IRQn          ((IRQn_Type)43)  /* USBHS FIFO 1 (DMA transfer request 1) */
#ifdef __cplusplus
}
#endif
#endif /* VECTOR_DATA_H */