/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __REGION_CN470_H__
#define __REGION_CN470_H__
#ifdef __cplusplus
extern "C"
{
#endif

#include "region/Region.h"

#if (REGION_VERSION <= REGION_VERSION_1_0_3_x)
    // LoRaWAN 1.0.3 Regional Parameters
    #include "region/RegionCN470_lw103rp.h"
#else
    // RP002-1.0.3 LoRaWAN Regional Parameters
    #include "region/RegionCN470_rp2103.h"
#endif

#ifdef __cplusplus
extern "C"
{
#endif
#endif  // _REGION_CN470_H__
