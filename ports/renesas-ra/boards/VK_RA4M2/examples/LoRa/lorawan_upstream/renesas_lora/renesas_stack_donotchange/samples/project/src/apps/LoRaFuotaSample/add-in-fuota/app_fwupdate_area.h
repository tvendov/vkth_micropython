/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __APP_FWUPDATE_AREA_H__
#define __APP_FWUPDATE_AREA_H__

#include "board.h"

/*----------*/
// swap mode
#define FUOTAUPDT_SWAPMODE_BOOTSWAP     0x01
#define FUOTAUPDT_SWAPMODE_BANKSWAP     0x02

#if defined(R7F100LPL)
    #ifndef FUOTAUPDT_SWAPMODE_CONFIG
    #define FUOTAUPDT_SWAPMODE_CONFIG       FUOTAUPDT_SWAPMODE_BANKSWAP  // default = BankSwap
    #endif
#endif

/*--- Configuration: Storage area of F/W image ---*/
// Must be same with definitions in FWUpdateSample (r_fuota_fwupdt_main.c).
// + FWUPDATE_CONFIG_STORAGEAREA_STARTADDR
//      Start address of storage area.
//      It must be in block size boundary.
//      In case of boot swap mode, it must be same with "R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR" in FWUpdateSample (r_fuota_fwupdt_main.c).
// + FWUPDATE_CONFIG_STORAGEAREA_SIZE
//      Size of storage area.
//      Do not specify it that exceeds the upper limit of end address of storage area.
//      In case of boot swap mode, it must be same with "R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE" in FWUpdateSample (r_fuota_fwupdt_main.c).
#if defined(R5F104ML) || defined(R5F104GL)
    //---------------------------------------------------------
    // RL78/G14 (R5F104ML, R5F104GL)
    //  + Block size of flash ROM                    : 1KB
    //  + Upper limit of end address of storage area : 0x7FDFF
    //---------------------------------------------------------
    #define FWUPDATE_CONFIG_STORAGEAREA_STARTADDR       0x040000
    #define FWUPDATE_CONFIG_STORAGEAREA_SIZE            0x03FE00
    // + FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT
    //      Please set the calculated value of "FWUPDATE_CONFIG_STORAGEAREA_STARTADDR - 4KB".
    //      (Ex.) 0x03F000 (= 0x040000 - 0x1000) in case of default config.
    //      It must be absolute address value (constant value). Expressions cannot be set.
    #define FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT   0x03F000

#elif defined(R5F104JJ)
    //---------------------------------------------------------
    // RL78/G14 (R5F104JJ)
    //  + Block size of flash ROM                    : 1KB
    //  + Upper limit of end address of storage area : 0x3FDFF
    //---------------------------------------------------------
    #define FWUPDATE_CONFIG_STORAGEAREA_STARTADDR       0x038000
    #define FWUPDATE_CONFIG_STORAGEAREA_SIZE            0x007E00
    // + FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT
    //      Please set the calculated value of "FWUPDATE_CONFIG_STORAGEAREA_STARTADDR - 4KB".
    //      (Ex.) 0x037000 (= 0x038000 - 0x1000) in case of default config.
    //      It must be absolute address value (constant value). Expressions cannot be set.
    #define FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT   0x037000

#elif defined(R7F100GSN)
    //---------------------------------------------------------
    // RL78/G23 (R7F100GSN)
    //  + Block size of flash ROM                    : 2KB
    //  + Upper limit of end address of storage area : 0x0BFDFF
    //---------------------------------------------------------
    #define FWUPDATE_CONFIG_STORAGEAREA_STARTADDR       0x040000
    #define FWUPDATE_CONFIG_STORAGEAREA_SIZE            0x07FE00
    // + FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT
    //      Please set the calculated value of "FWUPDATE_CONFIG_STORAGEAREA_STARTADDR - 8KB".
    //      (Ex.) 0x03E000 (= 0x040000 - 0x2000) in case of default config.
    //      It must be absolute address value (constant value). Expressions cannot be set.
    #define FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT   0x03E000

#elif defined(R7F100LPL)
  #if (FUOTAUPDT_SWAPMODE_CONFIG == FUOTAUPDT_SWAPMODE_BANKSWAP)
    //---------------------------------------------------------
    // RL78/L23 (R7F100LPL)  [Bank swap mode]
    //  + Block size of flash ROM                    : 2KB
    //  + Upper limit of end address of storage area : 0x3FDFF (startup bank)
    //  + bank size : 0x040000
    //    (Note) Actual storage area is in write bank.
    //           Therefore, start address of actual storage area is "FWUPDATE_CONFIG_STORAGEAREA_STARTADDR + 0x040000".
    //---------------------------------------------------------
    #define FWUPDATE_CONFIG_STORAGEAREA_STARTADDR       0x038000   //= 0x078000 (in write bank)
    #define FWUPDATE_CONFIG_STORAGEAREA_SIZE            0x007E00

  #elif (FUOTAUPDT_SWAPMODE_CONFIG == FUOTAUPDT_SWAPMODE_BOOTSWAP)
    //---------------------------------------------------------
    // RL78/L23 (R7F100LPL)  [Boot swap mode]
    //  + Block size of flash ROM                    : 2KB
    //  + Upper limit of end address of storage area : 0x7FDFF
    //---------------------------------------------------------
    #define FWUPDATE_CONFIG_STORAGEAREA_STARTADDR       0x040000
    #define FWUPDATE_CONFIG_STORAGEAREA_SIZE            0x03FE00
    // + FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT
    //      Please set the calculated value of "FWUPDATE_CONFIG_STORAGEAREA_STARTADDR - 8KB".
    //      (Ex.) 0x03E000 (= 0x040000 - 0x2000) in case of default config.
    //      It must be absolute address value (constant value). Expressions cannot be set.
    #define FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT   0x03E000

  #else
    #error "Error - invalid/unknown swap mode."
  #endif

#elif defined(R7FA2L1AB)
    //---------------------------------------------------------
    // RA2L1 (R7FA2L1AB)
    //  + Block size of flash ROM                    : 2KB
    //  + Upper limit of end address of storage area : 0x3BFFF
    //---------------------------------------------------------
    #define FWUPDATE_CONFIG_STORAGEAREA_STARTADDR       0x028000
    #define FWUPDATE_CONFIG_STORAGEAREA_SIZE            0x014000
    // + FWUPDT_EXT_LOC (in "{ProjectDir}\script\fsp_LoRaFuotaSample.ld")
    //      Please calculate the value of "FWUPDATE_CONFIG_STORAGEAREA_STARTADDR - 8KB".
    //      (Ex.) 0x026000 (= 0x028000 - 0x2000) in case of default config.
    //      If it is changed, please change "FWUPDT_EXT_LOC" value in {ProjectDir}\script\fsp_LoRaFuotaSample.ld.
#else
#error "Non-supported MCU."
#endif

/*----- ROM address definitions -----*/
#if defined(R5F104ML) || defined(R5F104GL)
    //----------------------------------
    // RL78/G14 (R5F104ML, R5F104GL)
    //----------------------------------
    #define FUOTAUPDT_SWAPMODE                     FUOTAUPDT_SWAPMODE_BOOTSWAP

    /*--- boot cluster 1 ; F/W update starup program ---*/
    #define FWUPDATE_STARTADDR_BCL1                 0x001000
    #define FWUPDATE_ENDADDR_BCL1                   0x001FFF
    #define FWUPDATE_SIZEOF_BCL1                    0x001000

    /*--- FlashROM ---*/
    #define FWUPDATE_CFLASH_BLOCKSIZE               1024  // 1KB/block

    /*--- end of ROM ---*/
    #define FWUPDATE_ENDADDRESS_ROM                 0x07FDFF

    /*--- Storage area of F/W image ---*/
    #define FWUPDATE_STARTADDR_STORAGEAREA          ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR )
    #define FWUPDATE_ENDADDR_STORAGEAREA            ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR + FWUPDATE_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (FWUPDATE_STARTADDR_STORAGEAREA % FWUPDATE_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - FWUPDATE_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( FWUPDATE_ENDADDR_STORAGEAREA > FWUPDATE_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

    /*--- Guard area which FWUpdateSample uses ---*/
    #define FWUPDATE_STARTADDR_AREA_FOR_FWUPDT      FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT
    #define FWUPDATE_SIZEOF_AREA_FOR_FWUPDT			( 4 * FWUPDATE_CFLASH_BLOCKSIZE )

#elif defined(R5F104JJ)
    //----------------------------------
    // RL78/G14 (R5F104JJ)
    //----------------------------------
    #define FUOTAUPDT_SWAPMODE                     FUOTAUPDT_SWAPMODE_BOOTSWAP

    /*--- boot cluster 1 ; F/W update starup program ---*/
    #define FWUPDATE_STARTADDR_BCL1                 0x001000
    #define FWUPDATE_ENDADDR_BCL1                   0x001FFF
    #define FWUPDATE_SIZEOF_BCL1                    0x001000

    /*--- FlashROM ---*/
    #define FWUPDATE_CFLASH_BLOCKSIZE               1024  // 1KB/block

    /*--- end of ROM ---*/
    #define FWUPDATE_ENDADDRESS_ROM                 0x03FDFF

    /*--- Storage area of F/W image ---*/
    #define FWUPDATE_STARTADDR_STORAGEAREA          ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR )
    #define FWUPDATE_ENDADDR_STORAGEAREA            ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR + FWUPDATE_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (FWUPDATE_STARTADDR_STORAGEAREA % FWUPDATE_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - FWUPDATE_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( FWUPDATE_ENDADDR_STORAGEAREA > FWUPDATE_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

    /*--- Guard area which FWUpdateSample uses ---*/
    #define FWUPDATE_STARTADDR_AREA_FOR_FWUPDT      FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT
    #define FWUPDATE_SIZEOF_AREA_FOR_FWUPDT			( 4 * FWUPDATE_CFLASH_BLOCKSIZE )

#elif defined(R7F100GSN)
    //----------------------------------
    // RL78/G23 (R7F100GSN)
    //----------------------------------
    #define FUOTAUPDT_SWAPMODE                     FUOTAUPDT_SWAPMODE_BOOTSWAP

    /*--- boot cluster 1 ; F/W update starup program ---*/
    #define FWUPDATE_STARTADDR_BCL1                 0x004000
    #define FWUPDATE_ENDADDR_BCL1                   0x007FFF
    #define FWUPDATE_SIZEOF_BCL1                    0x004000

    /*--- FlashROM ---*/
    #define FWUPDATE_CFLASH_BLOCKSIZE               2048  // 2KB/block

    /*--- end of ROM ---*/
    #define FWUPDATE_ENDADDRESS_ROM                 0x0BFDFF

    /*--- Storage area of F/W image ---*/
    #define FWUPDATE_STARTADDR_STORAGEAREA          ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR )
    #define FWUPDATE_ENDADDR_STORAGEAREA            ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR + FWUPDATE_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (FWUPDATE_STARTADDR_STORAGEAREA % FWUPDATE_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - FWUPDATE_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( FWUPDATE_ENDADDR_STORAGEAREA > FWUPDATE_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

    /*--- Guard area which FWUpdateSample uses ---*/
    #define FWUPDATE_STARTADDR_AREA_FOR_FWUPDT      FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT
    #define FWUPDATE_SIZEOF_AREA_FOR_FWUPDT			( 4 * FWUPDATE_CFLASH_BLOCKSIZE )

#elif defined(R7F100LPL)
    //----------------------------------
    // RL78/L23 (R7F100LPL)
    //----------------------------------
    #define FUOTAUPDT_SWAPMODE                     FUOTAUPDT_SWAPMODE_CONFIG

    /*--- FlashROM ---*/
    #define FWUPDATE_CFLASH_BLOCKSIZE               2048  // 2KB/block

  #if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
    /*--- Bank size ---*/
    #define FWUPDATE_BANKMODE_BANKSIZE              0x040000

    /*--- end of ROM (startup bank) ---*/
    #define FWUPDATE_ENDADDRESS_ROM                 0x03FDFF

    /*--- Storage area of F/W image ---*/
    // (note) Actual storate area is in write bank.
    //        Adding bank size to the following addresses are needed.
    #define FWUPDATE_STARTADDR_STORAGEAREA          ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR )
    #define FWUPDATE_ENDADDR_STORAGEAREA            ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR + FWUPDATE_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (FWUPDATE_STARTADDR_STORAGEAREA % FWUPDATE_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - FWUPDATE_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( FWUPDATE_ENDADDR_STORAGEAREA > FWUPDATE_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

  #elif (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BOOTSWAP)
    /*--- boot cluster 1 ; F/W update starup program ---*/
    #define FWUPDATE_STARTADDR_BCL1                 0x004000
    #define FWUPDATE_ENDADDR_BCL1                   0x007FFF
    #define FWUPDATE_SIZEOF_BCL1                    0x004000

    /*--- end of ROM ---*/
    #define FWUPDATE_ENDADDRESS_ROM                 0x07FDFF

    /*--- Storage area of F/W image ---*/
    #define FWUPDATE_STARTADDR_STORAGEAREA          ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR )
    #define FWUPDATE_ENDADDR_STORAGEAREA            ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR + FWUPDATE_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (FWUPDATE_STARTADDR_STORAGEAREA % FWUPDATE_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - FWUPDATE_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( FWUPDATE_ENDADDR_STORAGEAREA > FWUPDATE_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

    /*--- Guard area which FWUpdateSample uses ---*/
    #define FWUPDATE_STARTADDR_AREA_FOR_FWUPDT      FWUPDATE_CONFIG_STARTADDR_AREA_FOR_FWUPDT
    #define FWUPDATE_SIZEOF_AREA_FOR_FWUPDT			( 4 * FWUPDATE_CFLASH_BLOCKSIZE )

  #else
    #error "Error - invalid/unknown swap mode."
  #endif

#elif defined(R7FA2L1AB)
    //----------------------------------
    // RA2L1 (R7FA2L1AB)
    //----------------------------------
    #define FUOTAUPDT_SWAPMODE                     FUOTAUPDT_SWAPMODE_BOOTSWAP

    /*--- Alternate (Block1) ; F/W update starup program ---*/
    #define FWUPDATE_STARTADDR_BCL1                 0x002000
    #define FWUPDATE_ENDADDR_BCL1                   0x003FFF
    #define FWUPDATE_SIZEOF_BCL1                    0x002000

    /*--- FlashROM ---*/
    #define FWUPDATE_CFLASH_BLOCKSIZE               2048  // 2KB/block

    /*--- end of ROM ---*/
    #define FWUPDATE_ENDADDRESS_ROM                 0x03BFFF

    /*--- Storage area of F/W image ---*/
    #define FWUPDATE_STARTADDR_STORAGEAREA          ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR )
    #define FWUPDATE_ENDADDR_STORAGEAREA            ( FWUPDATE_CONFIG_STORAGEAREA_STARTADDR + FWUPDATE_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (FWUPDATE_STARTADDR_STORAGEAREA % FWUPDATE_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - FWUPDATE_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( FWUPDATE_ENDADDR_STORAGEAREA > FWUPDATE_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

    /*--- Guard area which FWUpdateSample uses ---*/
    #define FWUPDATE_SIZEOF_AREA_FOR_FWUPDT			( 4 * FWUPDATE_CFLASH_BLOCKSIZE )
    #define FWUPDATE_STARTADDR_AREA_FOR_FWUPDT      ( FWUPDATE_STARTADDR_STORAGEAREA - FWUPDATE_SIZEOF_AREA_FOR_FWUPDT )

#else
#error "Non-supported MCU."
#endif

#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BOOTSWAP)
/*--- Store address information of storage area and save area to ---*/
#define FWUPDATE_ADDRESSINFO_STORAGEAREA_START  ( (uint32_t)FWUPDATE_ENDADDR_BCL1 + 1 - 8 )
#define FWUPDATE_ADDRESSINFO_STORAGEAREA_END    ( (uint32_t)FWUPDATE_ENDADDR_BCL1 + 1 - 4 )
#endif


/*--- post include ---*/
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
#include "app_fuota_fwupdate_bank.h"
#endif

#endif  // __APP_FWUPDATE_AREA_H__
