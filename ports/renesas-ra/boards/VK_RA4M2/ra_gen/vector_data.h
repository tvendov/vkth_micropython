/* generated vector header file - do not edit */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
#define VECTOR_DATA_IRQ_COUNT    (40)
#endif
/* ISR prototypes */
void rtc_alarm_periodic_isr(void);
void rtc_carry_isr(void);
void spi_rxi_isr(void);
void spi_txi_isr(void);
void spi_tei_isr(void);
void spi_eri_isr(void);
void sci_uart_rxi_isr(void);
void sci_uart_txi_isr(void);
void sci_uart_tei_isr(void);
void sci_uart_eri_isr(void);
void r_icu_isr(void);
void usbfs_d0fifo_handler(void);
void usbfs_d1fifo_handler(void);
void usbfs_resume_handler(void);
void usbfs_interrupt_handler(void);
void iic_master_rxi_isr(void);
void iic_master_txi_isr(void);
void iic_master_tei_isr(void);
void iic_master_eri_isr(void);
void ctsu_write_isr(void);
void ctsu_read_isr(void);
void ctsu_end_isr(void);

/* Vector table allocations */
#define VECTOR_NUMBER_RTC_ALARM ((IRQn_Type)0)  /* RTC ALARM (Alarm interrupt) */
#define RTC_ALARM_IRQn          ((IRQn_Type)0)  /* RTC ALARM (Alarm interrupt) */
#define VECTOR_NUMBER_RTC_PERIOD ((IRQn_Type)1)  /* RTC PERIOD (Periodic interrupt) */
#define RTC_PERIOD_IRQn          ((IRQn_Type)1)  /* RTC PERIOD (Periodic interrupt) */
#define VECTOR_NUMBER_RTC_CARRY ((IRQn_Type)2)  /* RTC CARRY (Carry interrupt) */
#define RTC_CARRY_IRQn          ((IRQn_Type)2)  /* RTC CARRY (Carry interrupt) */
#define VECTOR_NUMBER_SPI0_RXI ((IRQn_Type)3)  /* SPI0 RXI (Receive buffer full) */
#define SPI0_RXI_IRQn          ((IRQn_Type)3)  /* SPI0 RXI (Receive buffer full) */
#define VECTOR_NUMBER_SPI0_TXI ((IRQn_Type)4)  /* SPI0 TXI (Transmit buffer empty) */
#define SPI0_TXI_IRQn          ((IRQn_Type)4)  /* SPI0 TXI (Transmit buffer empty) */
#define VECTOR_NUMBER_SPI0_TEI ((IRQn_Type)5)  /* SPI0 TEI (Transmission complete event) */
#define SPI0_TEI_IRQn          ((IRQn_Type)5)  /* SPI0 TEI (Transmission complete event) */
#define VECTOR_NUMBER_SPI0_ERI ((IRQn_Type)6)  /* SPI0 ERI (Error) */
#define SPI0_ERI_IRQn          ((IRQn_Type)6)  /* SPI0 ERI (Error) */
#define VECTOR_NUMBER_SCI2_RXI ((IRQn_Type)7)  /* SCI2 RXI (Received data full) */
#define SCI2_RXI_IRQn          ((IRQn_Type)7)  /* SCI2 RXI (Received data full) */
#define VECTOR_NUMBER_SCI2_TXI ((IRQn_Type)8)  /* SCI2 TXI (Transmit data empty) */
#define SCI2_TXI_IRQn          ((IRQn_Type)8)  /* SCI2 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI2_TEI ((IRQn_Type)9)  /* SCI2 TEI (Transmit end) */
#define SCI2_TEI_IRQn          ((IRQn_Type)9)  /* SCI2 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI2_ERI ((IRQn_Type)10)  /* SCI2 ERI (Receive error) */
#define SCI2_ERI_IRQn          ((IRQn_Type)10)  /* SCI2 ERI (Receive error) */
#define VECTOR_NUMBER_SCI9_RXI ((IRQn_Type)11)  /* SCI9 RXI (Received data full) */
#define SCI9_RXI_IRQn          ((IRQn_Type)11)  /* SCI9 RXI (Received data full) */
#define VECTOR_NUMBER_SCI9_TXI ((IRQn_Type)12)  /* SCI9 TXI (Transmit data empty) */
#define SCI9_TXI_IRQn          ((IRQn_Type)12)  /* SCI9 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI9_TEI ((IRQn_Type)13)  /* SCI9 TEI (Transmit end) */
#define SCI9_TEI_IRQn          ((IRQn_Type)13)  /* SCI9 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI9_ERI ((IRQn_Type)14)  /* SCI9 ERI (Receive error) */
#define SCI9_ERI_IRQn          ((IRQn_Type)14)  /* SCI9 ERI (Receive error) */
#define VECTOR_NUMBER_ICU_IRQ0 ((IRQn_Type)15)  /* ICU IRQ0 (External pin interrupt 0) */
#define ICU_IRQ0_IRQn          ((IRQn_Type)15)  /* ICU IRQ0 (External pin interrupt 0) */
#define VECTOR_NUMBER_USBFS_FIFO_0 ((IRQn_Type)16)  /* USBFS FIFO 0 (DMA transfer request 0) */
#define USBFS_FIFO_0_IRQn          ((IRQn_Type)16)  /* USBFS FIFO 0 (DMA transfer request 0) */
#define VECTOR_NUMBER_USBFS_FIFO_1 ((IRQn_Type)17)  /* USBFS FIFO 1 (DMA transfer request 1) */
#define USBFS_FIFO_1_IRQn          ((IRQn_Type)17)  /* USBFS FIFO 1 (DMA transfer request 1) */
#define VECTOR_NUMBER_USBFS_RESUME ((IRQn_Type)18)  /* USBFS RESUME (USBFS resume interrupt) */
#define USBFS_RESUME_IRQn          ((IRQn_Type)18)  /* USBFS RESUME (USBFS resume interrupt) */
#define VECTOR_NUMBER_USBFS_INT ((IRQn_Type)19)  /* USBFS INT (USBFS interrupt) */
#define USBFS_INT_IRQn          ((IRQn_Type)19)  /* USBFS INT (USBFS interrupt) */

#define VECTOR_NUMBER_CTSU_WRITE ((IRQn_Type)20)  /* CTSU WRITE (CTSU write request interrupt) */
#define CTSU_WRITE_IRQn          ((IRQn_Type)20)  /* CTSU WRITE (CTSU write request interrupt) */
#define VECTOR_NUMBER_CTSU_READ ((IRQn_Type)21)  /* CTSU READ (CTSU measurement data transfer request interrupt) */
#define CTSU_READ_IRQn          ((IRQn_Type)21)  /* CTSU READ (CTSU measurement data transfer request interrupt) */
#define VECTOR_NUMBER_CTSU_END ((IRQn_Type)22)  /* CTSU END (CTSU measurement end interrupt) */
#define CTSU_END_IRQn          ((IRQn_Type)22)  /* CTSU END (CTSU measurement end interrupt) */

#define VECTOR_NUMBER_ICU_IRQ1 ((IRQn_Type)23)  /* ICU IRQ1 (External pin interrupt 1) */
#define ICU_IRQ1_IRQn          ((IRQn_Type)23)  /* ICU IRQ1 (External pin interrupt 1) */
#define VECTOR_NUMBER_ICU_IRQ2 ((IRQn_Type)24)  /* ICU IRQ2 (External pin interrupt 2) */
#define ICU_IRQ2_IRQn          ((IRQn_Type)24)  /* ICU IRQ2 (External pin interrupt 2) */
#define VECTOR_NUMBER_ICU_IRQ3 ((IRQn_Type)25)  /* ICU IRQ3 (External pin interrupt 3) */
#define ICU_IRQ3_IRQn          ((IRQn_Type)25)  /* ICU IRQ3 (External pin interrupt 3) */
#define VECTOR_NUMBER_ICU_IRQ4 ((IRQn_Type)26)  /* ICU IRQ4 (External pin interrupt 4) */
#define ICU_IRQ4_IRQn          ((IRQn_Type)26)  /* ICU IRQ4 (External pin interrupt 4) */
#define VECTOR_NUMBER_ICU_IRQ5 ((IRQn_Type)27)  /* ICU IRQ5 (External pin interrupt 5) */
#define ICU_IRQ5_IRQn          ((IRQn_Type)27)  /* ICU IRQ5 (External pin interrupt 5) */
#define VECTOR_NUMBER_ICU_IRQ6 ((IRQn_Type)28)  /* ICU IRQ6 (External pin interrupt 6) */
#define ICU_IRQ6_IRQn          ((IRQn_Type)28)  /* ICU IRQ6 (External pin interrupt 6) */
#define VECTOR_NUMBER_ICU_IRQ7 ((IRQn_Type)29)  /* ICU IRQ7 (External pin interrupt 7) */
#define ICU_IRQ7_IRQn          ((IRQn_Type)29)  /* ICU IRQ7 (External pin interrupt 7) */
#define VECTOR_NUMBER_ICU_IRQ8 ((IRQn_Type)30)  /* ICU IRQ8 (External pin interrupt 8) */
#define ICU_IRQ8_IRQn          ((IRQn_Type)30)  /* ICU IRQ8 (External pin interrupt 8) */
#define VECTOR_NUMBER_ICU_IRQ9 ((IRQn_Type)31)  /* ICU IRQ9 (External pin interrupt 9) */
#define ICU_IRQ9_IRQn          ((IRQn_Type)31)  /* ICU IRQ9 (External pin interrupt 9) */

#define VECTOR_NUMBER_IIC0_RXI ((IRQn_Type)32)  /* IIC0 RXI (Receive data full) */
#define IIC0_RXI_IRQn          ((IRQn_Type)32)  /* IIC0 RXI (Receive data full) */
#define VECTOR_NUMBER_IIC0_TXI ((IRQn_Type)33)  /* IIC0 TXI (Transmit data empty) */
#define IIC0_TXI_IRQn          ((IRQn_Type)33)  /* IIC0 TXI (Transmit data empty) */
#define VECTOR_NUMBER_IIC0_TEI ((IRQn_Type)34)  /* IIC0 TEI (Transmit end) */
#define IIC0_TEI_IRQn          ((IRQn_Type)34)  /* IIC0 TEI (Transmit end) */
#define VECTOR_NUMBER_IIC0_ERI ((IRQn_Type)35)  /* IIC0 ERI (Transfer error) */
#define IIC0_ERI_IRQn          ((IRQn_Type)35)  /* IIC0 ERI (Transfer error) */
#define VECTOR_NUMBER_IIC1_RXI ((IRQn_Type)36)  /* IIC1 RXI (Receive data full) */
#define IIC1_RXI_IRQn          ((IRQn_Type)36)  /* IIC1 RXI (Receive data full) */
#define VECTOR_NUMBER_IIC1_TXI ((IRQn_Type)37)  /* IIC1 TXI (Transmit data empty) */
#define IIC1_TXI_IRQn          ((IRQn_Type)37)  /* IIC1 TXI (Transmit data empty) */
#define VECTOR_NUMBER_IIC1_TEI ((IRQn_Type)38)  /* IIC1 TEI (Transmit end) */
#define IIC1_TEI_IRQn          ((IRQn_Type)38)  /* IIC1 TEI (Transmit end) */
#define VECTOR_NUMBER_IIC1_ERI ((IRQn_Type)39)  /* IIC1 ERI (Transfer error) */
#define IIC1_ERI_IRQn          ((IRQn_Type)39)  /* IIC1 ERI (Transfer error) */

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_DATA_H */
