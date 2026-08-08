/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#include "app_fwupdate_area.h"

#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BOOTSWAP)
// guard BCL1; startup of F/W update program area
const uint8_t updateProgram0[ FWUPDATE_SIZEOF_BCL1 ];

// guard area which FWUpdateSample uses
const uint8_t useFWUpdateSample[ FWUPDATE_SIZEOF_AREA_FOR_FWUPDT ];

#endif
