.syntax unified
.cpu cortex-m33
.thumb

.section .vectors, "a", %progbits
.word 0x20020000
.word Reset_Handler + 1

.section .text.Reset_Handler, "ax", %progbits
.global Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    cpsid i
1:
    b 1b
