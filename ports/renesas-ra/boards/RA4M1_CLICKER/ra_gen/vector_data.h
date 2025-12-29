/* generated vector header file - do not edit */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H
#ifdef __cplusplus
extern "C" {
#endif
/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
/* RA4M1 provides 32 ICU vector slots. Here we use all of them: 24 for
 * non-ICU peripherals (SCI, RTC, AGT, SPI, IIC) and 8 for ICU external
 * interrupt channels (IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ9).
 */
#define VECTOR_DATA_IRQ_COUNT    (32)
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
void acmplp0_int_isr(void);
void acmplp1_int_isr(void);

/* Vector table allocations */
#define VECTOR_NUMBER_SCI0_RXI ((IRQn_Type)0)  /* SCI0 RXI (Receive data full) */
#define SCI0_RXI_IRQn          ((IRQn_Type)0)  /* SCI0 RXI (Receive data full) */
#define VECTOR_NUMBER_SCI0_TXI ((IRQn_Type)1)  /* SCI0 TXI (Transmit data empty) */
#define SCI0_TXI_IRQn          ((IRQn_Type)1)  /* SCI0 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI0_TEI ((IRQn_Type)2)  /* SCI0 TEI (Transmit end) */
#define SCI0_TEI_IRQn          ((IRQn_Type)2)  /* SCI0 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI0_ERI ((IRQn_Type)3)  /* SCI0 ERI (Receive error) */
#define SCI0_ERI_IRQn          ((IRQn_Type)3)  /* SCI0 ERI (Receive error) */
#define VECTOR_NUMBER_SCI1_RXI ((IRQn_Type)4)  /* SCI1 RXI (Received data full) */
#define SCI1_RXI_IRQn          ((IRQn_Type)4)  /* SCI1 RXI (Received data full) */
#define VECTOR_NUMBER_SCI1_TXI ((IRQn_Type)5)  /* SCI1 TXI (Transmit data empty) */
#define SCI1_TXI_IRQn          ((IRQn_Type)5)  /* SCI1 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI1_TEI ((IRQn_Type)6)  /* SCI1 TEI (Transmit end) */
#define SCI1_TEI_IRQn          ((IRQn_Type)6)  /* SCI1 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI1_ERI ((IRQn_Type)7)  /* SCI1 ERI (Receive error) */
#define SCI1_ERI_IRQn          ((IRQn_Type)7)  /* SCI1 ERI (Receive error) */
#define VECTOR_NUMBER_RTC_ALARM ((IRQn_Type)8)  /* RTC ALARM (Alarm interrupt) */
#define RTC_ALARM_IRQn          ((IRQn_Type)8)  /* RTC ALARM (Alarm interrupt) */
#define VECTOR_NUMBER_RTC_PERIOD ((IRQn_Type)9)  /* RTC PERIOD (Periodic interrupt) */
#define RTC_PERIOD_IRQn          ((IRQn_Type)9)  /* RTC PERIOD (Periodic interrupt) */
#define VECTOR_NUMBER_RTC_CARRY ((IRQn_Type)10)  /* RTC CARRY (Carry interrupt) */
#define RTC_CARRY_IRQn          ((IRQn_Type)10)  /* RTC CARRY (Carry interrupt) */
#define VECTOR_NUMBER_AGT0_INT ((IRQn_Type)11)  /* AGT0 INT (AGT interrupt) */
#define AGT0_INT_IRQn          ((IRQn_Type)11)  /* AGT0 INT (AGT interrupt) */
#define VECTOR_NUMBER_ICU_IRQ5 ((IRQn_Type)12)  /* ICU IRQ5 (External pin interrupt 5) */
#define ICU_IRQ5_IRQn          ((IRQn_Type)12)  /* ICU IRQ5 (External pin interrupt 5) */
#define VECTOR_NUMBER_SPI0_RXI ((IRQn_Type)13)  /* SPI0 RXI (Receive buffer full) */
#define SPI0_RXI_IRQn          ((IRQn_Type)13)  /* SPI0 RXI (Receive buffer full) */
#define VECTOR_NUMBER_SPI0_TXI ((IRQn_Type)14)  /* SPI0 TXI (Transmit buffer empty) */
#define SPI0_TXI_IRQn          ((IRQn_Type)14)  /* SPI0 TXI (Transmit buffer empty) */
#define VECTOR_NUMBER_SPI0_TEI ((IRQn_Type)15)  /* SPI0 TEI (Transmission complete event) */
#define SPI0_TEI_IRQn          ((IRQn_Type)15)  /* SPI0 TEI (Transmission complete event) */
#define VECTOR_NUMBER_SPI0_ERI ((IRQn_Type)16)  /* SPI0 ERI (Error) */
#define SPI0_ERI_IRQn          ((IRQn_Type)16)  /* SPI0 ERI (Error) */
#define VECTOR_NUMBER_IIC0_RXI ((IRQn_Type)17)  /* IIC0 RXI (Receive data full) */
#define IIC0_RXI_IRQn          ((IRQn_Type)17)  /* IIC0 RXI (Receive data full) */
#define VECTOR_NUMBER_IIC0_TXI ((IRQn_Type)18)  /* IIC0 TXI (Transmit data empty) */
#define IIC0_TXI_IRQn          ((IRQn_Type)18)  /* IIC0 TXI (Transmit data empty) */
#define VECTOR_NUMBER_IIC0_TEI ((IRQn_Type)19)  /* IIC0 TEI (Transmit end) */
#define IIC0_TEI_IRQn          ((IRQn_Type)19)  /* IIC0 TEI (Transmit end) */
#define VECTOR_NUMBER_IIC0_ERI ((IRQn_Type)20)  /* IIC0 ERI (Transfer error) */
#define IIC0_ERI_IRQn          ((IRQn_Type)20)  /* IIC0 ERI (Transfer error) */
#define VECTOR_NUMBER_IIC1_RXI ((IRQn_Type)21)  /* IIC1 RXI (Receive data full) */
#define IIC1_RXI_IRQn          ((IRQn_Type)21)  /* IIC1 RXI (Receive data full) */
#define VECTOR_NUMBER_IIC1_TXI ((IRQn_Type)22)  /* IIC1 TXI (Transmit data empty) */
#define IIC1_TXI_IRQn          ((IRQn_Type)22)  /* IIC1 TXI (Transmit data empty) */
#define VECTOR_NUMBER_IIC1_TEI ((IRQn_Type)23)  /* IIC1 TEI (Transmit end) */
#define IIC1_TEI_IRQn          ((IRQn_Type)23)  /* IIC1 TEI (Transmit end) */
#define VECTOR_NUMBER_IIC1_ERI ((IRQn_Type)24)  /* IIC1 ERI (Transfer error) */
#define IIC1_ERI_IRQn          ((IRQn_Type)24)  /* IIC1 ERI (Transfer error) */
#define VECTOR_NUMBER_ICU_IRQ6 ((IRQn_Type)25)  /* ICU IRQ6 (External pin interrupt 6) */
#define ICU_IRQ6_IRQn          ((IRQn_Type)25)  /* ICU IRQ6 (External pin interrupt 6) */
#define VECTOR_NUMBER_ICU_IRQ9 ((IRQn_Type)26)  /* ICU IRQ9 (External pin interrupt 9) */
#define ICU_IRQ9_IRQn          ((IRQn_Type)26)  /* ICU IRQ9 (External pin interrupt 9) */

/* Additional ICU external interrupt channels enabled for RA4M1 CLICKER.
 * Because RA4M1 only has 32 vector slots, we can enable at most 8
 * external interrupt channels concurrently. In addition to IRQ5, IRQ6
 * and IRQ9 above, we map IRQ0..IRQ2 here. The remaining two vector
 * slots (30 and 31) are reserved for ACMPLP comparator interrupts.
 */
#define VECTOR_NUMBER_ICU_IRQ0  ((IRQn_Type)27)  /* ICU IRQ0 (External pin interrupt 0) */
#define ICU_IRQ0_IRQn           ((IRQn_Type)27)  /* ICU IRQ0 (External pin interrupt 0) */
#define VECTOR_NUMBER_ICU_IRQ1  ((IRQn_Type)28)  /* ICU IRQ1 (External pin interrupt 1) */
#define ICU_IRQ1_IRQn           ((IRQn_Type)28)  /* ICU IRQ1 (External pin interrupt 1) */
#define VECTOR_NUMBER_ICU_IRQ2  ((IRQn_Type)29)  /* ICU IRQ2 (External pin interrupt 2) */
#define ICU_IRQ2_IRQn           ((IRQn_Type)29)  /* ICU IRQ2 (External pin interrupt 2) */
#define VECTOR_NUMBER_ACMPLP0_INT  ((IRQn_Type)30)  /* ACMPLP0 INT (Comparator 0 interrupt) */
#define ACMPLP0_INT_IRQn           ((IRQn_Type)30)  /* ACMPLP0 INT (Comparator 0 interrupt) */
#define VECTOR_NUMBER_ACMPLP1_INT  ((IRQn_Type)31)  /* ACMPLP1 INT (Comparator 1 interrupt) */
#define ACMPLP1_INT_IRQn           ((IRQn_Type)31)  /* ACMPLP1 INT (Comparator 1 interrupt) */
#ifdef __cplusplus
}
#endif
#endif /* VECTOR_DATA_H */
