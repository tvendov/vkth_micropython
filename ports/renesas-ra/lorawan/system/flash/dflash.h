/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __DFLASH_H__
#define __DFLASH_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "board.h"

#define DfLoadLargeSpace(a,b,c)     rp_veeprom_read(0,a,b)
#define DfSaveLargeSpace(a,b)       rp_veeprom_write(0,a,b)
#define DfLoadDFlash2Array(a,b,c)   {if(!rp_veeprom_read(0,a,b)){memcpy1((uint8_t *)a,c,b);}}
#define DfSaveArray2DFlash(a,b)     rp_veeprom_write(0,a,b)

bool rp_veeprom_read(uint8_t id, uint8_t * p_dst, size_t length);
bool rp_veeprom_write(uint8_t id, uint8_t * p_src, size_t length);
bool rp_veeprom_format(void);


#define RpMcuEepromRead(a, b, c)    rp_veeprom1_read(c, a, b)
#define RpMcuEepromWrite(a, b, c)   rp_veeprom1_write(c, a, b)
#define RpMcuEepromFormat()         rp_veeprom_format(); rp_veeprom1_format() 

bool rp_veeprom1_read(uint8_t id, uint8_t * p_dst, size_t length);
bool rp_veeprom1_write(uint8_t id, uint8_t * p_src, size_t length);
bool rp_veeprom1_format(void);

#ifdef __cplusplus
}
#endif

#endif /* __DFLASH_H__ */
