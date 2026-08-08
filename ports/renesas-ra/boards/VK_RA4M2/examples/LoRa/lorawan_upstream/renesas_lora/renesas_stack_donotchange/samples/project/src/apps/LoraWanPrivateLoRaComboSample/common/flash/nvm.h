/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __NVM_H__
#define __NVM_H__


/*----- return code -----*/
#define NVM_RESULT_SUCCESS        (0x00)
#define NVM_RESULT_FAILED         (0xFF)
#define NVM_RESULT_ERROR_NODATA   (0x10)

uint8_t NvmRead( uint8_t dataId, uint8_t *p_dataDst, uint8_t dataLen );
uint8_t NvmWrite( uint8_t dataId, uint8_t *p_dataSrc, uint8_t dataLen );

#endif // __R_NVM_H__

