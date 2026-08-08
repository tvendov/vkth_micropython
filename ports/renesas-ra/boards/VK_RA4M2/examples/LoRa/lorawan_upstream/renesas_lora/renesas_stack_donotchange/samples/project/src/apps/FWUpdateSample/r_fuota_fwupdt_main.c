/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
#endif

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "board.h"
#include "cflash.h"

#if defined(R5F104ML) || defined(R5F104GL) || defined(R5F104JJ)
/*--- change code section ---*/
#pragma section text    CFLSHCD
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
#pragma section const   DBGPRINTCNST
#endif
#endif

/*--- Configuration: Storage area of F/W image ---*/
// Must be same with definitions in LoRaFuotaSample (app_fwupdate_area.h).
// + R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR
//      Start address of storage area.
//      It must be in block size boundary.
//      It must be same with "FWUPDATE_CONFIG_STORAGEAREA_STARTADDR" in LoRaFuotaSample (app_fwupdate_area.h).
// + R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE
//      Size of storage area.
//      Do not specify it that exceeds the upper limit of end address of storage area.
//      It must be same with "FWUPDATE_CONFIG_STORAGEAREA_SIZE" in LoRaFuotaSample (app_fwupdate_area.h)
#if defined(R5F104ML) || defined(R5F104GL)
    //---------------------------------------------------------
    // RL78/G14 (R5F104ML, R5F104GL)
    //  + Block size of flash ROM                    : 1KB
    //  + Upper limit of end address of storage area : 0x7FDFF
    //---------------------------------------------------------
    #define R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR    0x040000
    #define R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE         0x03FE00
#elif defined(R5F104JJ)
    //---------------------------------------------------------
    // RL78/G14 (R5F104JJ)
    //  + Block size of flash ROM                    : 1KB
    //  + Upper limit of end address of storage area : 0x3FDFF
    //---------------------------------------------------------
    #define R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR    0x038000
    #define R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE         0x007E00
#elif defined(R7F100GSN)
    //---------------------------------------------------------
    // RL78/G23 (R7F100GSN)
    //  + Block size of flash ROM                    : 2KB
    //  + Upper limit of end address of storage area : 0x0BFDFF
    //---------------------------------------------------------
    #define R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR    0x040000
    #define R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE         0x07FE00
#elif defined(R7F100LPL)
    //---------------------------------------------------------
    // RL78/L23 (R7F100LPL)
    //  + Block size of flash ROM                    : 2KB
    //  + Upper limit of end address of storage area : 0x7FDFF
    //---------------------------------------------------------
    #define R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR    0x040000
    #define R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE         0x03FE00
#elif defined(R7FA2L1AB)
    //---------------------------------------------------------
    // RA2L1 (R7FA2L1AB)
    //  + Block size of flash ROM                    : 2KB
    //  + Upper limit of end address of storage area : 0x3BFFF
    //---------------------------------------------------------
    #define R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR    0x028000
    #define R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE         0x014000
#else
#error "Non-supported MCU."
#endif


/*----- ROM address definitions -----*/
#if defined(R5F104ML) || defined(R5F104GL)
    //----------------------------------
    // RL78/G14 (R5F104ML, R5F104GL)
    //----------------------------------
    /*--- boot cluster ---*/
    #define R_FUOTAUPDT_STARTADDR_BCL0                  0x000000
    #define R_FUOTAUPDT_ENDADDR_BCL0                    0x000FFF
    #define R_FUOTAUPDT_STARTADDR_BCL1                  0x001000
    #define R_FUOTAUPDT_ENDADDR_BCL1                    0x001FFF

    // guard BCL1
    #pragma address dummy_protectBcl1 = R_FUOTAUPDT_STARTADDR_BCL1
    const uint8_t dummy_protectBcl1[ 0x1000 ] = { 0xFF };  // not include in .mot file

    /*--- FlashROM ---*/
    #define R_FUOTAUPDT_CFLASH_BLOCKSIZE                1024    // 1KB/block

    /*--- end of ROM ---*/
    #define R_FUOTAUPDT_ENDADDRESS_ROM                  0x07FDFF

    /*--- Storage area of F/W image ---*/
    #define R_FUOTAUPDT_STARTADDR_STORAGEAREA           ( R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR )
    #define R_FUOTAUPDT_ENDADDR_STORAGEAREA             ( R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR + R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (R_FUOTAUPDT_STARTADDR_STORAGEAREA % R_FUOTAUPDT_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( R_FUOTAUPDT_ENDADDR_STORAGEAREA > R_FUOTAUPDT_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

    /*--- Save-area information ---*/                   // 0x03F000-0x03F3FF, 0x03F400-0x03F7FF, 0x03F800-0x03FBFF (in case default config)
    #define R_FUOTAUPDT_SAVEAREA_NUM_AREA               3
    #define R_FUOTAUPDT_SAVEAREA_STARTADDR              ( R_FUOTAUPDT_STARTADDR_STORAGEAREA - \
                                                          (R_FUOTAUPDT_CFLASH_BLOCKSIZE * (R_FUOTAUPDT_SAVEAREA_NUM_AREA + 1)) )
    #define R_FUOTAUPDT_SAVEAREA_ENDADDR                ( R_FUOTAUPDT_STARTADDR_STORAGEAREA - R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1 )

    /*--- Management-area address information ---*/     // 0x03FC00-0x03FFFF (in case default config)
    #define R_FUOTAUPDT_MNGAREA_STARTADDR               ( R_FUOTAUPDT_SAVEAREA_ENDADDR + 1 )
    #define R_FUOTAUPDT_MNGAREA_ENDADDR                 ( R_FUOTAUPDT_MNGAREA_STARTADDR + R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1 )

    /*--- Store address information of storage area and save area to ---*/
    #define R_FUOTAUPDT_ADDRESSINFO_STORAGEAREA_START   0x000FF8  // ( R_FUOTAUPDT_ENDADDR_BCL0 + 1 - 8 )
    #define R_FUOTAUPDT_ADDRESSINFO_STORAGEAREA_END     0x000FFC  // ( R_FUOTAUPDT_ENDADDR_BCL0 + 1 - 4 )

    /*--- (information for LoRaFuotaSample) ---*/
    // The range which FWUpdateSample uses;
    // ** Use 1 block (1KB) to allocate the FSL code
    // #define R_FUOTAUPDT_ADDRESSINFO_RESERVED_FOR_FWUPDTPRG_START    ( R_FUOTAUPDT_SAVEAREA_STARTADDR - R_FUOTAUPDT_CFLASH_BLOCKSIZE )
    // #define R_FUOTAUPDT_ADDRESSINFO_RESERVED_FOR_FWUPDTPRG_END      R_FUOTAUPDT_MNGAREA_ENDADDR

#elif defined(R5F104JJ)
    //----------------------------------
    // RL78/G14 (R5F104JJ)
    //----------------------------------
    /*--- boot cluster ---*/
    #define R_FUOTAUPDT_STARTADDR_BCL0                  0x000000
    #define R_FUOTAUPDT_ENDADDR_BCL0                    0x000FFF
    #define R_FUOTAUPDT_STARTADDR_BCL1                  0x001000
    #define R_FUOTAUPDT_ENDADDR_BCL1                    0x001FFF

    // guard BCL1
    #pragma address dummy_protectBcl1 = R_FUOTAUPDT_STARTADDR_BCL1
    const uint8_t dummy_protectBcl1[ 0x1000 ] = { 0xFF };  // not include in .mot file

    /*--- FlashROM ---*/
    #define R_FUOTAUPDT_CFLASH_BLOCKSIZE                1024    // 1KB/block

    /*--- end of ROM ---*/
    #define R_FUOTAUPDT_ENDADDRESS_ROM                  0x03FDFF

    /*--- Storage area of F/W image ---*/
    #define R_FUOTAUPDT_STARTADDR_STORAGEAREA           ( R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR )
    #define R_FUOTAUPDT_ENDADDR_STORAGEAREA             ( R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR + R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (R_FUOTAUPDT_STARTADDR_STORAGEAREA % R_FUOTAUPDT_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( R_FUOTAUPDT_ENDADDR_STORAGEAREA > R_FUOTAUPDT_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

    /*--- Save-area information ---*/                   // 0x037000-0x0373FF, 0x037400-0x0377FF, 0x037800-0x037BFF (in case default config)
    #define R_FUOTAUPDT_SAVEAREA_NUM_AREA               3
    #define R_FUOTAUPDT_SAVEAREA_STARTADDR              ( R_FUOTAUPDT_STARTADDR_STORAGEAREA - \
                                                          (R_FUOTAUPDT_CFLASH_BLOCKSIZE * (R_FUOTAUPDT_SAVEAREA_NUM_AREA + 1)) )
    #define R_FUOTAUPDT_SAVEAREA_ENDADDR                ( R_FUOTAUPDT_STARTADDR_STORAGEAREA - R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1 )

    /*--- Management-area address information ---*/     // 0x037C00-0x037FFF (in case default config)
    #define R_FUOTAUPDT_MNGAREA_STARTADDR               ( R_FUOTAUPDT_SAVEAREA_ENDADDR + 1 )
    #define R_FUOTAUPDT_MNGAREA_ENDADDR                 ( R_FUOTAUPDT_MNGAREA_STARTADDR + R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1 )

    /*--- Allocaate 4KB for CFlash Code to ---*/        // 0x036000-0x036FFF (in case default config)
    //*** Change section setting if R_FUOTAUPDT_SAVEAREA_NUM_AREA is changed from "3"
    #define R_FUOAUPDT_CFLASHCODE_STARTADDR             ( R_FUOTAUPDT_SAVEAREA_STARTADDR - 0x1000 )
    #define R_FUOAUPDT_CFLASHCODE_ENDADDR               ( R_FUOTAUPDT_SAVEAREA_STARTADDR - 1 )

    /*--- Store address information of storage area and save area to ---*/
    #define R_FUOTAUPDT_ADDRESSINFO_STORAGEAREA_START   0x000FF8  // ( R_FUOTAUPDT_ENDADDR_BCL0 + 1 - 8 )
    #define R_FUOTAUPDT_ADDRESSINFO_STORAGEAREA_END     0x000FFC  // ( R_FUOTAUPDT_ENDADDR_BCL0 + 1 - 4 )

    /*--- (information for LoRaFuotaSample) ---*/
    // The range which FWUpdateSample uses;
    // ** Use 1 block (1KB) to allocate the FSL code
    // #define R_FUOTAUPDT_ADDRESSINFO_RESERVED_FOR_FWUPDTPRG_START    ( R_FUOTAUPDT_SAVEAREA_STARTADDR - R_FUOTAUPDT_CFLASH_BLOCKSIZE )
    // #define R_FUOTAUPDT_ADDRESSINFO_RESERVED_FOR_FWUPDTPRG_END      R_FUOTAUPDT_MNGAREA_ENDADDR

#elif defined(R7F100GSN)
    //----------------------------------
    // RL78/G23 (R7F100GSN)
    //----------------------------------
    /*--- boot cluster ---*/
    #define R_FUOTAUPDT_STARTADDR_BCL0                  0x000000
    #define R_FUOTAUPDT_ENDADDR_BCL0                    0x003FFF
    #define R_FUOTAUPDT_STARTADDR_BCL1                  0x004000
    #define R_FUOTAUPDT_ENDADDR_BCL1                    0x007FFF

    // guard BCL1
    #pragma address dummy_protectBcl1 = R_FUOTAUPDT_STARTADDR_BCL1
    const uint8_t dummy_protectBcl1[ 0x4000 ] = { 0xFF };  // not include in .mot file

    /*--- FlashROM ---*/
    #define R_FUOTAUPDT_CFLASH_BLOCKSIZE                2048    // 2KB/block

    /*--- end of ROM ---*/
    #define R_FUOTAUPDT_ENDADDRESS_ROM                  0x0BFDFF

    /*--- Storage area of F/W image ---*/
    #define R_FUOTAUPDT_STARTADDR_STORAGEAREA           ( R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR )
    #define R_FUOTAUPDT_ENDADDR_STORAGEAREA             ( R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR + R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (R_FUOTAUPDT_STARTADDR_STORAGEAREA % R_FUOTAUPDT_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( R_FUOTAUPDT_ENDADDR_STORAGEAREA > R_FUOTAUPDT_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

    /*--- Save-area information ---*/                   // 0x03E000-0x03E7FF, 0x03E800-0x03EFFF, 0x03F000-0x03F7FF (in case default config)
    #define R_FUOTAUPDT_SAVEAREA_NUM_AREA               3
    #define R_FUOTAUPDT_SAVEAREA_STARTADDR              ( R_FUOTAUPDT_STARTADDR_STORAGEAREA - \
                                                          (R_FUOTAUPDT_CFLASH_BLOCKSIZE * (R_FUOTAUPDT_SAVEAREA_NUM_AREA + 1)) )
    #define R_FUOTAUPDT_SAVEAREA_ENDADDR                ( R_FUOTAUPDT_STARTADDR_STORAGEAREA - R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1 )

    /*--- Management-area address information ---*/     // 0x03F800-0x03FFFF (in case default config)
    #define R_FUOTAUPDT_MNGAREA_STARTADDR               ( R_FUOTAUPDT_SAVEAREA_ENDADDR + 1 )
    #define R_FUOTAUPDT_MNGAREA_ENDADDR                 ( R_FUOTAUPDT_MNGAREA_STARTADDR + R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1 )

    /*--- Store address information of storage area and save area to ---*/
    #define R_FUOTAUPDT_ADDRESSINFO_STORAGEAREA_START   0x003FF8  // ( R_FUOTAUPDT_ENDADDR_BCL0 + 1 - 8 )
    #define R_FUOTAUPDT_ADDRESSINFO_STORAGEAREA_END     0x003FFC  // ( R_FUOTAUPDT_ENDADDR_BCL0 + 1 - 4 )

    /*--- (information for LoRaFuotaSample) ---*/
    // The range which FWUpdateSample uses;
    // #define R_FUOTAUPDT_ADDRESSINFO_RESERVED_FOR_FWUPDTPRG_START    R_FUOTAUPDT_SAVEAREA_STARTADDR
    // #define R_FUOTAUPDT_ADDRESSINFO_RESERVED_FOR_FWUPDTPRG_END      R_FUOTAUPDT_MNGAREA_ENDADDR

#elif defined(R7F100LPL)
    //----------------------------------
    // RL78/L23 (R7F100LPL)
    //----------------------------------
    /*--- boot cluster ---*/
    #define R_FUOTAUPDT_STARTADDR_BCL0                  0x000000
    #define R_FUOTAUPDT_ENDADDR_BCL0                    0x003FFF
    #define R_FUOTAUPDT_STARTADDR_BCL1                  0x004000
    #define R_FUOTAUPDT_ENDADDR_BCL1                    0x007FFF

    // guard BCL1
    #pragma address dummy_protectBcl1 = R_FUOTAUPDT_STARTADDR_BCL1
    const uint8_t dummy_protectBcl1[ 0x4000 ] = { 0xFF };  // not include in .mot file

    /*--- FlashROM ---*/
    #define R_FUOTAUPDT_CFLASH_BLOCKSIZE                2048    // 2KB/block

    /*--- end of ROM ---*/
    #define R_FUOTAUPDT_ENDADDRESS_ROM                  0x07FDFF

    /*--- Storage area of F/W image ---*/
    #define R_FUOTAUPDT_STARTADDR_STORAGEAREA           ( R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR )
    #define R_FUOTAUPDT_ENDADDR_STORAGEAREA             ( R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR + R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (R_FUOTAUPDT_STARTADDR_STORAGEAREA % R_FUOTAUPDT_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( R_FUOTAUPDT_ENDADDR_STORAGEAREA > R_FUOTAUPDT_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

    /*--- Save-area information ---*/                   // 0x03E000-0x03E7FF, 0x03E800-0x03EFFF, 0x03F000-0x03F7FF (in case default config)
    #define R_FUOTAUPDT_SAVEAREA_NUM_AREA               3
    #define R_FUOTAUPDT_SAVEAREA_STARTADDR              ( R_FUOTAUPDT_STARTADDR_STORAGEAREA - \
                                                          (R_FUOTAUPDT_CFLASH_BLOCKSIZE * (R_FUOTAUPDT_SAVEAREA_NUM_AREA + 1)) )
    #define R_FUOTAUPDT_SAVEAREA_ENDADDR                ( R_FUOTAUPDT_STARTADDR_STORAGEAREA - R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1 )

    /*--- Management-area address information ---*/     // 0x03F800-0x03FFFF (in case default config)
    #define R_FUOTAUPDT_MNGAREA_STARTADDR               ( R_FUOTAUPDT_SAVEAREA_ENDADDR + 1 )
    #define R_FUOTAUPDT_MNGAREA_ENDADDR                 ( R_FUOTAUPDT_MNGAREA_STARTADDR + R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1 )

    /*--- Store address information of storage area and save area to ---*/
    #define R_FUOTAUPDT_ADDRESSINFO_STORAGEAREA_START   0x003FF8  // ( R_FUOTAUPDT_ENDADDR_BCL0 + 1 - 8 )
    #define R_FUOTAUPDT_ADDRESSINFO_STORAGEAREA_END     0x003FFC  // ( R_FUOTAUPDT_ENDADDR_BCL0 + 1 - 4 )

    /*--- (information for LoRaFuotaSample) ---*/
    // The range which FWUpdateSample uses;
    // #define R_FUOTAUPDT_ADDRESSINFO_RESERVED_FOR_FWUPDTPRG_START    R_FUOTAUPDT_SAVEAREA_STARTADDR
    // #define R_FUOTAUPDT_ADDRESSINFO_RESERVED_FOR_FWUPDTPRG_END      R_FUOTAUPDT_MNGAREA_ENDADDR

#elif defined(R7FA2L1AB)
    //----------------------------------
    // RA2L1 (R7FA2L1AB)
    //----------------------------------
    /*--- Default (Block0) Alternate (Block1) ---*/
    #define R_FUOTAUPDT_STARTADDR_BCL0                  0x000000
    #define R_FUOTAUPDT_ENDADDR_BCL0                    0x001FFF
    #define R_FUOTAUPDT_STARTADDR_BCL1                  0x002000
    #define R_FUOTAUPDT_ENDADDR_BCL1                    0x003FFF

    /*--- FlashROM ---*/
    #define R_FUOTAUPDT_CFLASH_BLOCKSIZE                2048    // 2KB/block

    /*--- end of ROM ---*/
    #define R_FUOTAUPDT_ENDADDRESS_ROM                  0x03BFFF

    /*--- Storage area of F/W image ---*/
    #define R_FUOTAUPDT_STARTADDR_STORAGEAREA           ( R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR )
    #define R_FUOTAUPDT_ENDADDR_STORAGEAREA             ( R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR + R_FUOTAUPDT_CONFIG_STORAGEAREA_SIZE - 1 )

    #if ( (R_FUOTAUPDT_STARTADDR_STORAGEAREA % R_FUOTAUPDT_CFLASH_BLOCKSIZE) != 0 )
    #error "Invalid config parameter - R_FUOTAUPDT_CONFIG_STORAGEAREA_STARTADDR is not in block size boundary."
    #endif
    #if ( R_FUOTAUPDT_ENDADDR_STORAGEAREA > R_FUOTAUPDT_ENDADDRESS_ROM )
    #error "Invalid config parameter - Storage area exceeds the upper limmit of the end address."
    #endif

    /*--- Save-area information ---*/                   // 0x026000-0x0267FF, 0x026800-0x026FFF, 0x027000-0x0277FF (in case default config)
    #define R_FUOTAUPDT_SAVEAREA_NUM_AREA               3
    #define R_FUOTAUPDT_SAVEAREA_STARTADDR              ( R_FUOTAUPDT_STARTADDR_STORAGEAREA - \
                                                          (R_FUOTAUPDT_CFLASH_BLOCKSIZE * (R_FUOTAUPDT_SAVEAREA_NUM_AREA + 1)) )
    #define R_FUOTAUPDT_SAVEAREA_ENDADDR                ( R_FUOTAUPDT_STARTADDR_STORAGEAREA - R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1 )

    /*--- Management-area address information ---*/     // 0x027800-0x027FFF (in case default config)
    #define R_FUOTAUPDT_MNGAREA_STARTADDR               ( R_FUOTAUPDT_SAVEAREA_ENDADDR + 1 )
    #define R_FUOTAUPDT_MNGAREA_ENDADDR                 ( R_FUOTAUPDT_MNGAREA_STARTADDR + R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1 )

    /*--- Store address information of storage area and save area to ---*/
    #define R_FUOTAUPDT_ADDRESSINFO_STORAGEAREA_START   0x001FF8  // ( R_FUOTAUPDT_ENDADDR_BCL0 + 1 - 8 )
    #define R_FUOTAUPDT_ADDRESSINFO_STORAGEAREA_END     0x001FFC  // ( R_FUOTAUPDT_ENDADDR_BCL0 + 1 - 4 )

    /*--- (information for LoRaFuotaSample) ---*/
    // The range which FWUpdateSample uses;
    // #define R_FUOTAUPDT_ADDRESSINFO_RESERVED_FOR_FWUPDTPRG_START    R_FUOTAUPDT_SAVEAREA_STARTADDR
    // #define R_FUOTAUPDT_ADDRESSINFO_RESERVED_FOR_FWUPDTPRG_END      R_FUOTAUPDT_MNGAREA_ENDADDR

#else
#error "Non-supported MCU."
#endif

const uint32_t g_addrInfo_storage_start      = R_FUOTAUPDT_STARTADDR_STORAGEAREA;
const uint32_t g_addrInfo_storage_end        = R_FUOTAUPDT_ENDADDR_STORAGEAREA;

/*--- state machine ---*/
enum {
    R_FUOTAUPDT_STATE_INIT = 0,
    R_FUOTAUPDT_STATE_CHECK_FWIMG,
    R_FUOTAUPDT_STATE_VERIFY,
    R_FUOTAUPDT_STATE_RECOVER_UPDATE,
    R_FUOTAUPDT_STATE_UPDATE,
    R_FUOTAUPDT_STATE_SUCCESS_BTSWAP,
    R_FUOTAUPDT_STATE_ERROR_BTSWAP,
    R_FUOTAUPDT_STATE_ERROR_REBOOT
};

uint8_t g_fuotaupdtState;

extern int app_main( void );

static void R_FuotaUpdt_Init( void );
static void R_FuotaUpdt_CheckFwImage( void );
static void R_FuotaUpdt_Verify( void );
static void R_FuotaUpdt_RecoveryUpdate( void );
static void R_FuotaUpdt_FirmwareUpdate( void );
static void R_FuotaUpdt_SuccessUpdateBootswap( void );
static void R_FuotaUpdt_ErrorUpdateBootswap( void );
static void R_FuotaUpdt_ErrorUpdateReboot( void );

/*--- F/W image ---*/
typedef struct {
    uint8_t     imageBlockNum;
    uint8_t     imageBlockIndex;  // must be 0
    uint8_t     __pad32bit[2];    //*** caution; explicitly define the padding. MUST BE 0
    uint32_t    imageVersion;
    uint32_t    imageSize;
    uint8_t     imagePriority;
    uint8_t     _reserved;        // for alignment
    uint8_t     imageVerify[32];
    // (must be even size)
} FuotaUpdtImageInfo_t;

#define R_FUOTAUPDT_SIZEOF_FWIMGINFO            (44)
#define R_FUOTAUPDT_FWIMGINFO_IMGVERIFY_AREA    (12)
#define R_FUOTAUPDT_FWIMGINFO_IMGVERIFY_SIZE    (32)
FuotaUpdtImageInfo_t    g_tmpFwImageInfo;

FuotaUpdtImageInfo_t *gp_fuotaupdtImageInfo;

/*--- F/W update; image blocks ---*/
typedef struct {
    uint32_t    _imageBlockStartAddr;   // it is not included in image block format
    uint8_t     imageBlockNum;
    uint8_t     imageBlockIndex;
    uint8_t     __pad32bit[2];    //*** caution; explicitly define the padding. MUST BE 0
    uint32_t    codeAddress;
    uint32_t    codeSize;
    uint8_t     *p_code;  // access code by pointer
} FuotaUpdtImageBlock_t;

FuotaUpdtImageBlock_t   g_tmpImageBlock;

static void R_FuotaUpdt_GetNextImageBlock( FuotaUpdtImageBlock_t **pp_nextBlock );
static bool R_FuotaUpdt_FirmwareUpdate_WriteVerify( FuotaUpdtImageBlock_t *p_imageBlock );

/*--- Save area ---*/
uint32_t    ga_saveAreaStartAddr[ R_FUOTAUPDT_SAVEAREA_NUM_AREA ];

/*--- Update information in management area ---*/
typedef struct {  // do not change the order
    uint16_t    dstFlashBlockNo;    //= flash address / (1024 or 2048)
    uint16_t    dstFlashBlockNoForVerify;
    uint32_t    isUpdateEnded;
} FuotaUpdateProcInfo_t;

FuotaUpdateProcInfo_t *gp_updateProcInfoBase;         // pointer to management area (ROM)
FuotaUpdateProcInfo_t *gp_updateProcInfoToRecover;    // pointer to management area (ROM)
uint16_t                        g_saveAreaIndexToRecover;

#define R_FUOTAUPDT_MNGAREA_BLOCKWRITE_NOT_ENDED    0xFFFFFFFF
#define R_FUOTAUPDT_MNGAREA_BLOCKWRITE_ENDED        0x55555555
#define R_FUOTAUPDT_MNGAREA_NUM_UPDTINFO    ( R_FUOTAUPDT_CFLASH_BLOCKSIZE / sizeof(FuotaUpdateProcInfo_t) )

/*--- Flash ---*/
#define R_FUOTAUPDT_CFLASH_BLOCK_STARTADDR( cfAddr )        ( (cfAddr) & (uint32_t)(~(R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1)) )
#define R_FUOTAUPDT_CFLASH_BLOCK_RELATIVEADDR( cfAddr )     ( (cfAddr) & (uint32_t)(R_FUOTAUPDT_CFLASH_BLOCKSIZE - 1) )
#define R_FUOTAUPDT_CFLASH_BLOCK_NO( cfAddr )               ( (cfAddr) / (uint32_t)R_FUOTAUPDT_CFLASH_BLOCKSIZE )

uint8_t g_FlashBlockRamTemp[ R_FUOTAUPDT_CFLASH_BLOCKSIZE ];

/*---  ---*/
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
#if defined(R7F100GSN)
extern void RpMcuUartInit( void );
#endif
#endif
#ifdef DEBUG_FUOTAUPDT
#ifndef DEBUG_FUOTAUPDT_CFG_HOOK_POST_RECOVER_FW
#define DEBUG_FUOTAUPDT_CFG_HOOK_POST_RECOVER_FW
#endif
#ifndef DEBUG_FUOTAUPDT_CFG_HOOK_PRE_WRITE_FW
#define DEBUG_FUOTAUPDT_CFG_HOOK_PRE_WRITE_FW
#endif
#define DEBUG_FUOTAUPDT_HOOK_POST_RECOVER_FW( arg ) DEBUG_FUOTAUPDT_CFG_HOOK_POST_RECOVER_FW( arg )
#define DEBUG_FUOTAUPDT_HOOK_PRE_WRITE_FW( arg )    DEBUG_FUOTAUPDT_CFG_HOOK_PRE_WRITE_FW( arg )
#endif


/**
 * Main application entry point.
 */
int app_main( void )
{
#if defined(R7F100GSN) || defined(R7F100LPL)
    // RFD Section initialization
    R_CFlash_InitSct();
#endif

    // Target board initialization
    BoardInitMcu( );
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
#if defined(R7F100GSN) || defined(R7F100LPL)
    RpMcuUartInit();
#endif
#endif

    // init
    g_fuotaupdtState = R_FUOTAUPDT_STATE_INIT;

    while(1)
    {
        switch( g_fuotaupdtState )
        {
            case R_FUOTAUPDT_STATE_INIT:
                R_FuotaUpdt_Init();
                break;

            case R_FUOTAUPDT_STATE_CHECK_FWIMG:
                R_FuotaUpdt_CheckFwImage();
                break;

            case R_FUOTAUPDT_STATE_VERIFY:
                R_FuotaUpdt_Verify();
                break;

            case R_FUOTAUPDT_STATE_RECOVER_UPDATE:
                R_FuotaUpdt_RecoveryUpdate();
                break;

            case R_FUOTAUPDT_STATE_UPDATE:
                R_FuotaUpdt_FirmwareUpdate();
                break;

            case R_FUOTAUPDT_STATE_SUCCESS_BTSWAP:
                R_FuotaUpdt_SuccessUpdateBootswap();
                break;

            case R_FUOTAUPDT_STATE_ERROR_BTSWAP:
                R_FuotaUpdt_ErrorUpdateBootswap();
                break;

            case R_FUOTAUPDT_STATE_ERROR_REBOOT:
                R_FuotaUpdt_ErrorUpdateReboot();
                break;

            default:
                break;
        }
    }
}

/*----------------------------------------------------------------------------*/
// State machine

/*
 *  Init
 */
static void R_FuotaUpdt_Init( void )
{
    FuotaUpdateProcInfo_t *p_updtProcInfo;
    FuotaUpdateProcInfo_t       blankUpdtProcInfo;
    uint16_t                    i;
    int                         comp;

    R_CFlash_Update_Init();

    // set write protect area (BCL0, stored area, and end of ROM)
    R_CFlash_AddWriteProtectArea( R_FUOTAUPDT_STARTADDR_BCL0, R_FUOTAUPDT_ENDADDR_BCL0 );
    R_CFlash_AddWriteProtectArea( g_addrInfo_storage_start, g_addrInfo_storage_end );
    R_CFlash_AddWriteProtectArea( (R_FUOTAUPDT_ENDADDRESS_ROM + 1), 0xFFFFFFFF );

    // init valiables first
    //  - save area
    for( i = 0; i < R_FUOTAUPDT_SAVEAREA_NUM_AREA; i++ )
    {
        ga_saveAreaStartAddr[ i ] = (uint32_t)( R_FUOTAUPDT_SAVEAREA_STARTADDR + ( R_FUOTAUPDT_CFLASH_BLOCKSIZE * i ) );
    }

    //  - FW update process info
    gp_updateProcInfoBase = (FuotaUpdateProcInfo_t *)R_FUOTAUPDT_MNGAREA_STARTADDR;

    // Check whether recovery of flash write is necessary
    blankUpdtProcInfo.dstFlashBlockNo          = (uint16_t)0xFFFF;
    blankUpdtProcInfo.dstFlashBlockNoForVerify = (uint16_t)0xFFFF;
    blankUpdtProcInfo.isUpdateEnded            = (uint32_t)0xFFFFFFFF;
    p_updtProcInfo = gp_updateProcInfoBase;
    for( i = 0; i < R_FUOTAUPDT_MNGAREA_NUM_UPDTINFO; i++, p_updtProcInfo++ )
    {
        comp = memcmp( p_updtProcInfo, &blankUpdtProcInfo, sizeof(FuotaUpdateProcInfo_t) );
        if( comp == 0 )  //= blank entry
        {
            continue;  // continue for(i) loop
        }

        // (re-)init information to recovery
        gp_updateProcInfoToRecover = NULL;
        g_saveAreaIndexToRecover   = (uint16_t)(-1);  // not used

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
        print( "CheckRecover: updtProcInfo[" );
        print_dec( i, 3, '0');
        print( "]:" );

        print( "dstBlkNo=0x" );
        print_hex( p_updtProcInfo->dstFlashBlockNo, 4 );
        print( "(0x" );
        print_hex( p_updtProcInfo->dstFlashBlockNoForVerify, 4 );
        print( "), " );
        print( "ended=0x" );
        print_hex( p_updtProcInfo->isUpdateEnded, 8 );
        print_newline();
#endif
        // Maybe recovery is needed.
        if( ( p_updtProcInfo->dstFlashBlockNo == p_updtProcInfo->dstFlashBlockNoForVerify ) &&
            ( p_updtProcInfo->isUpdateEnded == R_FUOTAUPDT_MNGAREA_BLOCKWRITE_NOT_ENDED ) )
        {
            // Recovery of one block write is needed if p_updtProcInfo is the latest entry. Keep the entry.
            gp_updateProcInfoToRecover = p_updtProcInfo;
            g_saveAreaIndexToRecover   = i % R_FUOTAUPDT_SAVEAREA_NUM_AREA;
        }
    }

    // Erase management area if recovery is not needed
    if( gp_updateProcInfoToRecover == NULL )
    {
        (void)R_CFlash_EraseBlock( R_FUOTAUPDT_MNGAREA_STARTADDR );
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
        print( "CheckRecover: Not required" );
        print_newline();
        print_newline();
    }
    else
    {
        print( "CheckRecover: Required" );
        print_newline();
        print_newline();
#endif
    }
    // next state
    g_fuotaupdtState = R_FUOTAUPDT_STATE_CHECK_FWIMG;
}

/*
 *  Check F/W image
 */
static void R_FuotaUpdt_CheckFwImage( void )
{
    uint8_t                     res;
    FuotaUpdtImageBlock_t *p_imgBlock;
    uint32_t                    addressFwImgEnd;
    uint32_t                    cfAddr;
    uint32_t                    tmpAddrEnd;
    uint32_t                    storedImageSize;
    uint8_t                     i;
    uint8_t                     *p_tmp8;

    // init
    res = R_CFLASH_RESULT_SUCCESS;

    // get F/W image
    p_tmp8 = (uint8_t *)g_addrInfo_storage_start;

    memset( &g_tmpFwImageInfo, 0x00, R_FUOTAUPDT_SIZEOF_FWIMGINFO );
    g_tmpFwImageInfo.imageBlockNum = (*p_tmp8++);
    g_tmpFwImageInfo.imageBlockIndex = (*p_tmp8++);
    // g_tmpFwImageInfo.imageVersion has been cleared
    for( i = 0; i < 4; i++ )
    {
        g_tmpFwImageInfo.imageVersion |= (uint32_t)(*p_tmp8++) << (i*8);
    }
    // g_tmpFwImageInfo.imageSize has been cleared
    for( i = 0; i < 4; i++ )
    {
        g_tmpFwImageInfo.imageSize |= (uint32_t)(*p_tmp8++) << (i*8);
    }
    g_tmpFwImageInfo.imagePriority = (*p_tmp8++);
    g_tmpFwImageInfo._reserved     = (*p_tmp8++);
    memcpy( g_tmpFwImageInfo.imageVerify, p_tmp8, sizeof(g_tmpFwImageInfo.imageVerify) );

    gp_fuotaupdtImageInfo = &g_tmpFwImageInfo;
    addressFwImgEnd       = g_addrInfo_storage_start + gp_fuotaupdtImageInfo->imageSize - 1;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    print( "GetInfo: F/W image" );
    print_newline();

    print( "GetInfo:   - Stored area   : 0x" );
    print_hex( g_addrInfo_storage_start, 8 );
    print( " - 0x" );
    print_hex( addressFwImgEnd, 8 );
    print_newline();

    print( "GetInfo:   - imageBlockNum : " );
    print_dec( gp_fuotaupdtImageInfo->imageBlockNum, 3, '\0' );
    print_newline();

    print( "GetInfo:   - imageVersion  : 0x" );
    print_hex( gp_fuotaupdtImageInfo->imageVersion, 8 );
    print_newline();

    print( "GetInfo:   - imageSize     : 0x" );
    print_hex( gp_fuotaupdtImageInfo->imageSize, 8 );
    print_newline();

    print( "GetInfo:   - Priority      : " );
    print_dec( gp_fuotaupdtImageInfo->imagePriority, 3, '\0' );
    print_newline();
#endif

    // check; stored area of F/W image
    if( ( addressFwImgEnd <= g_addrInfo_storage_start ) ||
        ( g_addrInfo_storage_start <= R_FUOTAUPDT_ENDADDR_BCL1 ) ||
        ( addressFwImgEnd > g_addrInfo_storage_end ) )
    {
        // stored area of F/W image is invalid.
        res = R_CFLASH_RESULT_FAILED;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
        print( "GetInfo: *** error; stored area" );
        print_newline();
#endif
    }

    // check; ImageBlockNum must be greater than 0
    if( res == R_CFLASH_RESULT_SUCCESS )
    {
        if( gp_fuotaupdtImageInfo->imageBlockNum == 0 )
        {
            res = R_CFLASH_RESULT_FAILED;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
            print( "GetInfo: *** error; imageBlockNum" );
            print_newline();
#endif
        }

    }

    // check; CodeAddress and CodeSize
    if( res == R_CFLASH_RESULT_SUCCESS )
    {
        p_imgBlock = NULL;  // init to get 1st entry
        for( i = 1; i <= gp_fuotaupdtImageInfo->imageBlockNum; i++ )  // imageBlockIndex = 1,2,...,N
        {
            R_FuotaUpdt_GetNextImageBlock( &p_imgBlock );
            cfAddr     = p_imgBlock->codeAddress;
            tmpAddrEnd = cfAddr + p_imgBlock->codeSize - 1;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
            print( "GetInfo: ImageBlock#" );
            print_dec( p_imgBlock->imageBlockIndex, 3, '\0' );
            print( " of " );
            print_dec( p_imgBlock->imageBlockNum, 3, '\0' );
            print_newline();

            print( "GetInfo:   - CodeAddress : 0x" );
            print_hex( cfAddr, 8 );
            print( " - 0x" );
            print_hex( tmpAddrEnd, 8 );
            print_newline();

            print( "GetInfo:   - CodeSize    : 0x" );
            print_hex( p_imgBlock->codeSize, 8 );
            print_newline();
#endif

            // check image block num/index
            if( ( p_imgBlock->imageBlockNum != gp_fuotaupdtImageInfo->imageBlockNum ) ||
                ( p_imgBlock->imageBlockIndex != i ) )
            {
                res = R_CFLASH_RESULT_FAILED;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
                print( "GetInfo: *** error; imageBlockNum/Index" );
                print_newline();
#endif
                break;  // exit for(i) loop
            }

            // check destination address; NOT boot claster 1
            if( ( tmpAddrEnd >= R_FUOTAUPDT_STARTADDR_BCL1 ) &&
                ( cfAddr <= R_FUOTAUPDT_ENDADDR_BCL1 ) )
            {
                res = R_CFLASH_RESULT_FAILED;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
                print( "GetInfo: *** error; CodeAddress (in BCL1)" );
                print_newline();
#endif
                break;  // exit for(i) loop
            }

            // check destination address; NOT SaveArea/StoredArea
            if( tmpAddrEnd >= R_FUOTAUPDT_SAVEAREA_STARTADDR )
            {
                res = R_CFLASH_RESULT_FAILED;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
                print( "GetInfo: *** error; CodeAddress (in protected area)" );
                print_newline();
#endif
                break;  // exit for(i) loop
            }
        }
    }

    // check; total size of  F/W image
    if( res == R_CFLASH_RESULT_SUCCESS )
    {
        R_FuotaUpdt_GetNextImageBlock( &p_imgBlock );
        storedImageSize = p_imgBlock->_imageBlockStartAddr - g_addrInfo_storage_start;

        if( storedImageSize != gp_fuotaupdtImageInfo->imageSize )
        {
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
            print( "GetInfo: *** error; wrong ImageSize" );
            print_newline();
#endif
            res = R_CFLASH_RESULT_FAILED;
        }
    }

    // next state
    if( res == R_CFLASH_RESULT_SUCCESS )
    {
        g_fuotaupdtState = R_FUOTAUPDT_STATE_VERIFY;
    }
    else
    {
        g_fuotaupdtState = R_FUOTAUPDT_STATE_ERROR_BTSWAP;
    }
}

/*
 *  Verify F/W image
 */
static void R_FuotaUpdt_Verify( void )
{
    uint32_t                    i;
    uint8_t *p_fwImg;
    FuotaUpdtImageInfo_t *p_fwImgInfo;
    uint32_t                    calcChksum, verifyChksum;

    // init
    p_fwImgInfo  = gp_fuotaupdtImageInfo;
    p_fwImg      = (uint8_t *)g_addrInfo_storage_start;

    // calculate check sum
    calcChksum = 0;
    for( i = 0; i < p_fwImgInfo->imageSize; i++ )
    {
        if( ( i < R_FUOTAUPDT_FWIMGINFO_IMGVERIFY_AREA ) ||
            ( i >= (R_FUOTAUPDT_FWIMGINFO_IMGVERIFY_AREA + R_FUOTAUPDT_FWIMGINFO_IMGVERIFY_SIZE) ) )
        {
            calcChksum += (*p_fwImg);
        }

        p_fwImg++;
    }

    // get check sum
    verifyChksum = (uint32_t)( p_fwImgInfo->imageVerify[0] ) +
                   ( (uint32_t)( p_fwImgInfo->imageVerify[1] ) <<  8 ) + 
                   ( (uint32_t)( p_fwImgInfo->imageVerify[2] ) << 16 ) + 
                   ( (uint32_t)( p_fwImgInfo->imageVerify[3] ) << 24 );

    // next state
    if( calcChksum == verifyChksum )
    {
        g_fuotaupdtState = R_FUOTAUPDT_STATE_RECOVER_UPDATE;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
        print( "Verify: Checksum OK" );
        print_newline();
#endif
    }
    else
    {
        g_fuotaupdtState = R_FUOTAUPDT_STATE_ERROR_BTSWAP;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
        print( "Verify: *** error; Checksum NG" );
        print_newline();
#endif
    }
}

/*
 *  Recovery of one block write
 */
static void R_FuotaUpdt_RecoveryUpdate( void )
{
    uint8_t     funcRet;
    uint32_t    addrSrc, addrDst;

    // init
    funcRet = R_CFLASH_RESULT_SUCCESS;

    if( gp_updateProcInfoToRecover != NULL )
    {
        // write flash from save area to update area
        addrSrc = ga_saveAreaStartAddr[ g_saveAreaIndexToRecover ];
        addrDst = (uint32_t)gp_updateProcInfoToRecover->dstFlashBlockNo * R_FUOTAUPDT_CFLASH_BLOCKSIZE;
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
        print( "Recovery: Execute - " );
        print( "save[" );
        print_dec( g_saveAreaIndexToRecover, 1, '\0' );
        print( "](0x" );
        print_hex( addrSrc, 8 );
        print( ") -> Blk.0x" );
        print_hex( gp_updateProcInfoToRecover->dstFlashBlockNo, 4 );
        print( "(0x" );
        print_hex( addrDst, 8 );
        print( ")" );
        print_newline();
#endif
        funcRet = R_CFlash_Update_WriteData_inBlock( addrDst,
                                                     R_FUOTAUPDT_CFLASH_BLOCKSIZE,
                                                     (uint8_t *)addrSrc,
                                                     1 );
        if( funcRet == R_CFLASH_RESULT_SUCCESS )
        {
            // erase management area
            (void)R_CFlash_EraseBlock( R_FUOTAUPDT_MNGAREA_STARTADDR );

            gp_updateProcInfoToRecover = NULL;
#ifdef DEBUG_FUOTAUPDT
            DEBUG_FUOTAUPDT_HOOK_POST_RECOVER_FW( addrDst );
#endif
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
            print( "Recovery: OK" );
            print_newline();
        }
        else
        {
            print( "Recovery: NG" );
            print_newline();
#endif
        }
    }
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    else
    {
        print( "Recovery: skip (not necessary)" );
        print_newline();
    }
#endif

    // next state
    if( funcRet == R_CFLASH_RESULT_SUCCESS )
    {
        g_fuotaupdtState = R_FUOTAUPDT_STATE_UPDATE;
    }
    else
    {
        g_fuotaupdtState = R_FUOTAUPDT_STATE_ERROR_REBOOT;
    }
}

/*
 *  F/W update
 */
static void R_FuotaUpdt_FirmwareUpdate( void )
{
    FuotaUpdtImageBlock_t *p_imageBlockToUpdate;
    uint8_t                     funcRet;
    uint32_t                    blockAddrToWrite, relativeBlkAddr, blockAddrNext;
    uint32_t                    codeAddrToWrite;
    uint32_t                    codeSizeRemained, writeSize;
    uint8_t *p_imgBlkCode;
    bool                        isBlockMerged, isUpdateWholeBlock, isErrorUpdate;
    bool                        isWrVerifyOk;
    // save area
    uint8_t                     saveAreaIndex;
    // update info / management area
    FuotaUpdateProcInfo_t       updateInfo;
    uint32_t                    mngAreaAddr, tmpSize32;

    // init
    saveAreaIndex = 0;
    mngAreaAddr   = R_FUOTAUPDT_MNGAREA_STARTADDR;
    isErrorUpdate = false;
    isWrVerifyOk  = false;

    //---------------------------------
    // Write FWImage image block(s)

    // init before do-while() loop
    p_imageBlockToUpdate = NULL;  // init to get 1st entry
    R_FuotaUpdt_GetNextImageBlock( &p_imageBlockToUpdate );  // get image block #1

    codeAddrToWrite  = p_imageBlockToUpdate->codeAddress;
    if( codeAddrToWrite <= R_FUOTAUPDT_ENDADDR_BCL0 )
    {
        // startup (boot cluster 0) code will be written to the boot cluster 1
        codeAddrToWrite += R_FUOTAUPDT_STARTADDR_BCL1;
    }
    codeSizeRemained = p_imageBlockToUpdate->codeSize;
    p_imgBlkCode     = p_imageBlockToUpdate->p_code;

    do
    {
        // Copy destination block to RAM.
        blockAddrToWrite = R_FUOTAUPDT_CFLASH_BLOCK_STARTADDR( codeAddrToWrite );
        memcpy( &g_FlashBlockRamTemp[ 0 ], (uint8_t *)blockAddrToWrite, R_FUOTAUPDT_CFLASH_BLOCKSIZE );

        // merge image block(s)
        isBlockMerged      = false;
        isUpdateWholeBlock = false;
        do
        {
            // merge image block with destination block RAM
            relativeBlkAddr = R_FUOTAUPDT_CFLASH_BLOCK_RELATIVEADDR( codeAddrToWrite );
            if( (relativeBlkAddr + codeSizeRemained) > R_FUOTAUPDT_CFLASH_BLOCKSIZE )
            {
                // continue to the next flash block
                writeSize = R_FUOTAUPDT_CFLASH_BLOCKSIZE - relativeBlkAddr;
            }
            else
            {
                writeSize = codeSizeRemained;
            }
            memcpy( &g_FlashBlockRamTemp[ relativeBlkAddr ], p_imgBlkCode, writeSize );

            if( writeSize == R_FUOTAUPDT_CFLASH_BLOCKSIZE )
            {
                isUpdateWholeBlock = true;
            }

            codeSizeRemained -= writeSize;
            if( codeSizeRemained > 0 )
            {
                // (for next flash block) use same image block. update current image block info
                codeAddrToWrite += writeSize;
                p_imgBlkCode    += writeSize;

                isBlockMerged = true;
            }
            else
            {
                // get next image block
                if( p_imageBlockToUpdate->imageBlockIndex < p_imageBlockToUpdate->imageBlockNum )
                {
                    R_FuotaUpdt_GetNextImageBlock( &p_imageBlockToUpdate );  // get next image block

                    codeAddrToWrite = p_imageBlockToUpdate->codeAddress;
                    if( codeAddrToWrite <= R_FUOTAUPDT_ENDADDR_BCL0 )
                    {
                        // startup (boot cluster 0) code will be written to the boot cluster 1
                        codeAddrToWrite += R_FUOTAUPDT_STARTADDR_BCL1;
                    }
                    codeSizeRemained = p_imageBlockToUpdate->codeSize;
                    p_imgBlkCode     = p_imageBlockToUpdate->p_code;

                    // check whether next image block is for current flash block
                    blockAddrNext = R_FUOTAUPDT_CFLASH_BLOCK_STARTADDR( codeAddrToWrite );
                    if( blockAddrNext != blockAddrToWrite )
                    {
                        // next image block is not for current flash block
                        isBlockMerged = true;
                    }
                }
                else
                {
                    // no more image block
                    p_imageBlockToUpdate = NULL;
                    isBlockMerged = true;
                }
            }
        } while( isBlockMerged == false );

        //--------------------
        // Update

        if( isUpdateWholeBlock == false )
        {
            // Save update code to save area (when it update the patr of block)
            funcRet = R_CFlash_Update_WriteData_inBlock( ga_saveAreaStartAddr[ saveAreaIndex ], 
                                                         R_FUOTAUPDT_CFLASH_BLOCKSIZE, 
                                                         &g_FlashBlockRamTemp[ 0 ], 
                                                         1 );
            if( funcRet != R_CFLASH_RESULT_SUCCESS )
            {
                isErrorUpdate = true;
                break;  // exit do-while(p_imageBlockToUpdate) loop
            }

            // Write update information to Management area (when it update the patr of block)
            if( ( mngAreaAddr + sizeof(updateInfo) ) > ( R_FUOTAUPDT_MNGAREA_ENDADDR + 1 ) )
            {
                // Erase management area and reset address because it exceeds range of the area
                (void)R_CFlash_EraseBlock( R_FUOTAUPDT_MNGAREA_STARTADDR );
                mngAreaAddr = R_FUOTAUPDT_MNGAREA_STARTADDR;
            }
            updateInfo.dstFlashBlockNo          = (uint16_t)R_FUOTAUPDT_CFLASH_BLOCK_NO( blockAddrToWrite );
            updateInfo.dstFlashBlockNoForVerify = updateInfo.dstFlashBlockNo;
            tmpSize32 = (uint32_t)(&(updateInfo.isUpdateEnded)) - (uint32_t)(&updateInfo);
            funcRet = R_CFlash_Update_WriteData_inBlock( mngAreaAddr,
                                                         tmpSize32,
                                                         (uint8_t *)&updateInfo,
                                                         0 );
            if( funcRet != R_CFLASH_RESULT_SUCCESS )
            {
                isErrorUpdate = true;
                break;  // exit do-while(p_imageBlockToUpdate) loop
            }
            mngAreaAddr += tmpSize32;
        }

        // Update firmware
#ifdef DEBUG_FUOTAUPDT
        DEBUG_FUOTAUPDT_HOOK_PRE_WRITE_FW( blockAddrToWrite );
#endif
        funcRet = R_CFlash_Update_WriteData_inBlock( blockAddrToWrite, 
                                                     R_FUOTAUPDT_CFLASH_BLOCKSIZE, 
                                                     &g_FlashBlockRamTemp[ 0 ], 
                                                     1 );
        if( funcRet != R_CFLASH_RESULT_SUCCESS )
        {
            isErrorUpdate = true;
            break;  // exit do-while(p_imageBlockToUpdate) loop
        }

        if( isUpdateWholeBlock == false )
        {
            // Write update information to Management area (when it update the patr of block)
            updateInfo.isUpdateEnded = R_FUOTAUPDT_MNGAREA_BLOCKWRITE_ENDED;
            funcRet = R_CFlash_Update_WriteData_inBlock( mngAreaAddr,
                                                        sizeof(updateInfo.isUpdateEnded),
                                                        (uint8_t *)&(updateInfo.isUpdateEnded),
                                                        0 );
            if( funcRet != R_CFLASH_RESULT_SUCCESS )
            {
                isErrorUpdate = true;
                break;  // exit do-while(p_imageBlockToUpdate) loop
            }
            mngAreaAddr += sizeof(updateInfo.isUpdateEnded);

            // next
            saveAreaIndex++;
            if( saveAreaIndex >= R_FUOTAUPDT_SAVEAREA_NUM_AREA )
            {
                saveAreaIndex = 0;  // for next
            }
        }
    } while( p_imageBlockToUpdate != NULL );
    
    // Verify
    if( isErrorUpdate == false )
    {
        p_imageBlockToUpdate = NULL;  // init to get 1st entry
        do
        {
            // get image block #n (n=1...N)
            R_FuotaUpdt_GetNextImageBlock( &p_imageBlockToUpdate );

            // verify
            isWrVerifyOk = R_FuotaUpdt_FirmwareUpdate_WriteVerify( p_imageBlockToUpdate );
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
            if( isWrVerifyOk == true )
            {
                // verify OK
                print( "Update: ImageBlock#" );
                print_dec( p_imageBlockToUpdate->imageBlockIndex, 3, '\0' );
                print( " ... OK" );
                print_newline();
            }
            else
            {
                // verify NG
                print( "Update: ImageBlock#" );
                print_dec( p_imageBlockToUpdate->imageBlockIndex, 3, '\0' );
                print( " ... NG(verify error)" );
                print_newline();
            }
#endif
            if( isWrVerifyOk != true )
            {
                break;  // exit do-while() loop
            }
        } while( p_imageBlockToUpdate->imageBlockIndex < p_imageBlockToUpdate->imageBlockNum );
    }

    // next state
    if( isWrVerifyOk == true )
    {
        g_fuotaupdtState = R_FUOTAUPDT_STATE_SUCCESS_BTSWAP;
    }
    else
    {
        g_fuotaupdtState = R_FUOTAUPDT_STATE_ERROR_REBOOT;
    }
}

/*
 *  Success: Bootswap
 */
#ifndef DEBUG_FUOTAUPDT
static void R_FuotaUpdt_SuccessUpdateBootswap( void )
{
    uint8_t     funcRet;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    print( "Finished: SUCCESS. Switch program by bootswap" );
    print_newline();
#endif

    funcRet = R_CFlash_SwitchBootCluster();

    // only comes here if bootswap is failed. => retry as error.
    if( funcRet == R_CFLASH_RESULT_FAILED )
    {
        g_fuotaupdtState = R_FUOTAUPDT_STATE_ERROR_BTSWAP;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
        print( "Finished: *** error; bootswap" );
        print_newline();
#endif
    }
}
#else  //--- for stand-alone debug ---//
static void R_FuotaUpdt_SuccessUpdateBootswap( void )
{
    // Erase management area before bootswap
    // (void)R_CFlash_EraseBlock( R_FUOTAUPDT_MNGAREA_STARTADDR );

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    print( "Finished: SUCCESS. Switch program by bootswap (now in infinite loop for debug)" );
    print_newline();
#endif

    while(1);
}
#endif

/*
 *  Failed: Bootswap
 */
#ifndef DEBUG_FUOTAUPDT
static void R_FuotaUpdt_ErrorUpdateBootswap( void )
{
    uint8_t     funcRet;

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    print( "Finished: Failed. Switch program by bootswap" );
    print_newline();
#endif

    funcRet = R_CFlash_SwitchBootCluster();

    // only comes here if bootswap is failed. => retry
    if( funcRet == R_CFLASH_RESULT_FAILED )
    {
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
        print( "Finished: *** error; bootswap" );
        print_newline();
#endif
    }
}
#else  //--- for stand-alone debug ---//
static void R_FuotaUpdt_ErrorUpdateBootswap( void )
{
    // Erase management area before bootswap
    // (void)R_CFlash_EraseBlock( R_FUOTAUPDT_MNGAREA_STARTADDR );

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    print( "Finished: Failed. Switch program by bootswap (now in infinite loop for debug)" );
    print_newline();
#endif

    while(1);
}
#endif

/*
 *  Failed: Reboot
 */
#ifndef DEBUG_FUOTAUPDT
static void R_FuotaUpdt_ErrorUpdateReboot( void )
{
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    print( "Finished: Failed. Reboot my program" );
    print_newline();
#endif

    R_CFlash_ResetFW();
    while(1);
}
#else  //--- for stand-alone debug ---//
static void R_FuotaUpdt_ErrorUpdateReboot( void )
{
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    print( "Finished: Failed. Reboot my program (now in infinite loop for debug)" );
    print_newline();
#endif

    while(1);
}
#endif


/*----------------------------------------------------------------------------*/
// Sub functions for ImageBlock
/*
 *  ImageBlock: get pointer to next block
 *    (warning; The number of image blocks is not taken into account.)
 */
static void R_FuotaUpdt_GetNextImageBlock( FuotaUpdtImageBlock_t **pp_nextBlock )
{
    uint32_t    nextImgBlockAddr;
    uint8_t     isEndedImgBlk;
    uint8_t     i, *p_tmp8;

    // init
    isEndedImgBlk = 0;

    if( (*pp_nextBlock) == NULL )
    {
        // 1st image block
        nextImgBlockAddr  = g_addrInfo_storage_start;
        nextImgBlockAddr += R_FUOTAUPDT_SIZEOF_FWIMGINFO;
    }
    else
    {
        // next image block   // +10 = sizeof(imageBlockNum + imageBlockIndex + codeAddress + codeSize)
        nextImgBlockAddr = (*pp_nextBlock)->_imageBlockStartAddr + (*pp_nextBlock)->codeSize + 10;
        nextImgBlockAddr = (nextImgBlockAddr + 1) & 0xFFFFFFFE;  // even address

        if( (*pp_nextBlock)->imageBlockIndex == (*pp_nextBlock)->imageBlockNum )
        {
            isEndedImgBlk = 1;  // non-zero
        }
    }

    memset( &g_tmpImageBlock, 0x00, sizeof(FuotaUpdtImageBlock_t) );
    g_tmpImageBlock._imageBlockStartAddr = nextImgBlockAddr;

    if( isEndedImgBlk == 0 )
    {
        p_tmp8 = (uint8_t *)nextImgBlockAddr;

        g_tmpImageBlock.imageBlockNum   = (*p_tmp8++);
        g_tmpImageBlock.imageBlockIndex = (*p_tmp8++);
        // g_tmpImageBlock.codeAddress has been cleared
        for( i = 0; i < 4; i++ )
        {
            g_tmpImageBlock.codeAddress |= (uint32_t)(*p_tmp8++) << (i*8);
        }
        // g_tmpImageBlock.codeSize has been cleared
        for( i = 0; i < 4; i++ )
        {
            g_tmpImageBlock.codeSize |= (uint32_t)(*p_tmp8++) << (i*8);
        }
        g_tmpImageBlock.p_code = p_tmp8;
    }

    (*pp_nextBlock) = &g_tmpImageBlock;
}


/*
 *  Write verify update
 */
static bool R_FuotaUpdt_FirmwareUpdate_WriteVerify( FuotaUpdtImageBlock_t *p_imageBlock )
{
    bool            bRes;
    uint32_t        cfAddr;
    uint8_t *p_src, *p_dst;
    uint32_t        i;
 
    // init
    bRes = true;

    // get destination address
    cfAddr = p_imageBlock->codeAddress;
    if( cfAddr <= R_FUOTAUPDT_ENDADDR_BCL0 )
    {
        // startup (boot cluster 0) code will be written to the boot cluster 1
        cfAddr += R_FUOTAUPDT_STARTADDR_BCL1;
    }

    // verify
    p_src = p_imageBlock->p_code;
    p_dst = (uint8_t *)cfAddr;

    for( i = 0; i < p_imageBlock->codeSize; i++ )
    {
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
        if( ( i % 16 ) == 0 )
        {
            print_newline();
            print( "Update: 0x" );
            print_hex( (uint32_t)p_src, 8 );
            print( "->0x" );
            print_hex( (uint32_t)p_dst, 8 );
            print( " : " );
        }
#endif

        if( (*p_src) != (*p_dst) )
        {
            bRes = false;
            break;  // exit for(i) loop
        }

#ifdef DEBUG_FUOTAUPDT_DBGPRINT
        print_hex( (*p_src), 2 );
#endif

        // next
        p_src++;
        p_dst++;
    }

    // result
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    if( bRes == false )
    {
        print_newline();
        print( "Update: verify error." );
        print( " [src]0x" );
        print_hex( (uint32_t)p_src, 8 );
        print( "=" );
        print_hex( (*p_src), 2 );
        print(" [dst]0x" );
        print_hex( (uint32_t)p_dst, 8 );
        print( "=" );
        print_hex( (*p_dst), 2 );
    }
    print_newline();
#endif

    return bRes;
}
