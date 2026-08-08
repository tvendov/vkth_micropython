/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2017 Semtech

Description: 	Firmware update over the air with LoRa proof of concept
				Functions for the decoding
*/
/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.

    Changes:
        - Changed from C++ based source code to C source code.
        - Changed names such as functions, variable, macros and file names.
        - Changed data type of variables to reduce memory.
        - Limited acceptable maximum number of missed frames
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "timer.h"
#include "LoRaMac.h"

#include "LoRaFragmentProcess.h"
#include "LoraFragmentFec.h"

#define FRGMNT_FEC_MATRIX_SIZE_ROWS_BYTE        ( (FRGMNT_CONFIG_MAX_NBFRAG + 7) / 8 )
#define FRGMNT_FEC_MATRIX_SIZE_LOST_ROWS_BYTE   ( (FRGMNT_CONFIG_MAX_NBFRAG_LOST + 7) / 8 )

/*** FEC management ***/
typedef struct {
    uint16_t    missedNth;
    uint8_t     m2b[ FRGMNT_FEC_MATRIX_SIZE_LOST_ROWS_BYTE ];
} Frgmnt_MissTable_t;

typedef struct {
    uint8_t             fragIndex_fecmng; // fragmentation session index
    uint16_t            nbFrag;
    uint8_t             fragSize;
    uint16_t            fragMissingNum;
    uint8_t             tableS[ FRGMNT_FEC_MATRIX_SIZE_LOST_ROWS_BYTE ];
    Frgmnt_MissTable_t  tableMiss[ FRGMNT_CONFIG_MAX_NBFRAG_LOST ];
    uint16_t            m2l;
} Frgmnt_FecMng_t;
Frgmnt_FecMng_t FrgmntFecMng[ FRGMNT_CONFIG_MAX_FRAG_INDEX ];
#define FRGMNT_FEC_FRAGINDEX_NOUSE  0xFF


// matrix access
static uint8_t LoRaFrgmntFecMatrixLineBitGet( uint16_t index, uint8_t *p_matrix, uint16_t matrixArraySize );
static void LoRaFrgmntFecMatrixLineBitSet( uint16_t index, 
                                           uint8_t  bitval, 
                                           uint8_t  *p_matrix, 
                                           uint16_t matrixArraySize );
static uint16_t LoRaFrgmntFecMatrixLineBitSearch( uint16_t startIndex, 
                                                  uint8_t  searchBitval, 
                                                  uint8_t  *p_matrix, 
                                                  uint16_t matrixArraySize );
static void LoRaFrgmntFecMatrixLineXor( uint8_t  *p_matrix1, 
                                        uint8_t  *p_matrix2, 
                                        uint16_t matrixArraySize );
static bool LoRaFrgmntFecMatrixLineIsNull( uint8_t *p_matrix, uint16_t matrixArraySize );

// make Parity matrix
static void LoRaFrgmntFecGetParityMatrixRow( uint16_t fragNth, 
                                             uint16_t M_nbFrag,
                                             uint8_t  *p_matrix, 
                                             uint16_t matrixArraySize );
static uint32_t LoRaFrgmntFecPrbs23( uint32_t x_start );
// Frame
static void LoRaFrgmntFecXorFrame( uint8_t *p_data1, uint8_t *p_data2, uint8_t size );

// Mng
static void LoRaFrgmntInitFrgmntFecMng( Frgmnt_FecMng_t *p_frgmntFecMng );
static Frgmnt_FecMng_t* LoRaFrgmntSearchFrgmntFecMng( uint8_t fragIndex );
static Frgmnt_FecMng_t* LoRaFrgmntGetFrgmntFecMng( uint8_t fragIndex );
static void LoRaFrgmntReleaseFrgmntFecMng( Frgmnt_FecMng_t *p_frgmntFecMng );

// else
#define FRGMNT_FEC_MACRO_DATABLK_OFFSET_PTR( p_datablk, nth, fragSize )    ( (p_datablk) + ( ( (nth) - 1 ) * (fragSize) ) )

//-------------------------------------------------------------------------------------------------

/*!
 * FragmentDataBlock - FEC initialization
 */
void LoRaFragmentFecInit( void )
{
    Frgmnt_FecMng_t     *p_frgmntFecMng;
    uint8_t             i;

    for( i = 0; i < FRGMNT_CONFIG_MAX_FRAG_INDEX; i++ )
    {
        p_frgmntFecMng = &FrgmntFecMng[ i ];
        LoRaFrgmntReleaseFrgmntFecMng( p_frgmntFecMng );
    }
}

/*!
 * FragmentDataBlock - Setup to start
 */
FrgmntStatus_t LoRaFragmentFecSetup( uint8_t fragIndex, uint16_t nbFrag, uint8_t fragSize )
{
    Frgmnt_FecMng_t *p_frgmntFecMng;

    // init
    p_frgmntFecMng = NULL;

    // get FrgmntFecMng
    p_frgmntFecMng = LoRaFrgmntGetFrgmntFecMng( fragIndex );
    if( p_frgmntFecMng == NULL )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // initialize FrgmntFecMng
    LoRaFrgmntInitFrgmntFecMng( p_frgmntFecMng );
    p_frgmntFecMng->nbFrag   = nbFrag;
    p_frgmntFecMng->fragSize = fragSize;

    return FRGMNT_STATUS_OK;
}

/*!
 * FragmentDataBlock - set missed information for FEC
 */
FrgmntStatus_t LoRaFragmentFecMissedUncoded( uint8_t fragIndex, uint16_t missedNth )
{
    FrgmntStatus_t  res;
    Frgmnt_FecMng_t *p_frgmntFecMng;

    // init
    res            = FRGMNT_STATUS_OK;
    p_frgmntFecMng = NULL;

    // get frgmntFecMng
    p_frgmntFecMng = LoRaFrgmntSearchFrgmntFecMng( fragIndex );
    if( p_frgmntFecMng == NULL )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // set missing to missing matrix
    if( p_frgmntFecMng->fragMissingNum < FRGMNT_CONFIG_MAX_NBFRAG_LOST )
    {
        p_frgmntFecMng->tableMiss[ p_frgmntFecMng->fragMissingNum ].missedNth = missedNth;
        p_frgmntFecMng->fragMissingNum += 1;
    }
    else
    {
        res = FRGMNT_STATUS_ERROR;
    }

    return res;
}


/*!
 * @fn
 * Process for redundant frame
 * @return  result code
 */
uint16_t LoRaFragmentFecProcessRedundancy( uint8_t  fragIndex, 
                                           uint16_t fragNth, 
                                           uint8_t  *p_redundancyFrame,
                                           uint8_t  *p_dataBlockBuffer )
{
    Frgmnt_FecMng_t *p_frgmntFecMng;
    uint8_t         fecParityMatrixLine[ FRGMNT_FEC_MATRIX_SIZE_ROWS_BYTE ];
    uint8_t         fecMissMatrixLine[ FRGMNT_FEC_MATRIX_SIZE_LOST_ROWS_BYTE ];  // maxsize = (fragMissingNum + 7) / 8
    uint8_t         fecM2bTmp[ FRGMNT_FEC_MATRIX_SIZE_LOST_ROWS_BYTE ];   // maxsize = (fragMissingNum + 7) / 8
    uint8_t         fecXorData[ 256 ];
    uint8_t         *p_dataBlkOffset;
    int16_t         i, j;
    bool            isMissFound;
    uint8_t         bitVal;
    uint16_t        missTblIndex, firstMissTblIndex;
    uint16_t        binaryMatrixSize;
    bool            noMoreInfo;

    // get frgmntFecMng
    p_frgmntFecMng = NULL;  // init
    p_frgmntFecMng = LoRaFrgmntSearchFrgmntFecMng( fragIndex );
    if( p_frgmntFecMng == NULL )
    {
        return FRGMNT_FEC_STATUS_ONGOING;  // (fail-safe) Never comes here usually.
    }

    // init
    memcpy1( fecXorData, p_redundancyFrame, p_frgmntFecMng->fragSize );

    memset1( fecMissMatrixLine, 0x00, FRGMNT_FEC_MATRIX_SIZE_LOST_ROWS_BYTE );
    memset1( fecM2bTmp, 0x00, FRGMNT_FEC_MATRIX_SIZE_LOST_ROWS_BYTE );
    binaryMatrixSize = ( p_frgmntFecMng->fragMissingNum + 7 ) / 8;

    // get corrsponding line of parity check matrix
    memset1( fecParityMatrixLine, 0x00, FRGMNT_FEC_MATRIX_SIZE_ROWS_BYTE );
    LoRaFrgmntFecGetParityMatrixRow( fragNth, 
                                     p_frgmntFecMng->nbFrag, 
                                     fecParityMatrixLine, 
                                     FRGMNT_FEC_MATRIX_SIZE_ROWS_BYTE );

    missTblIndex = 1;
    isMissFound  = false;
    for( i = 1; i <= p_frgmntFecMng->nbFrag; i++ )
    {
        if( p_frgmntFecMng->tableMiss[ missTblIndex - 1 ].missedNth < i )
        {
            if( missTblIndex < FRGMNT_CONFIG_MAX_NBFRAG_LOST )
            {
                missTblIndex += 1;
            }
        }

        bitVal = LoRaFrgmntFecMatrixLineBitGet( i, fecParityMatrixLine, 
                                                FRGMNT_FEC_MATRIX_SIZE_ROWS_BYTE );
        if( bitVal == 1 )
        {
            if( p_frgmntFecMng->tableMiss[ missTblIndex - 1 ].missedNth != i )
            {
                // xor with already receive frame
                LoRaFrgmntFecMatrixLineBitSet( i, 0, fecParityMatrixLine, 
                                               FRGMNT_FEC_MATRIX_SIZE_ROWS_BYTE );
                p_dataBlkOffset = FRGMNT_FEC_MACRO_DATABLK_OFFSET_PTR( p_dataBlockBuffer, 
                                                                       i, p_frgmntFecMng->fragSize );
                LoRaFrgmntFecXorFrame( fecXorData, p_dataBlkOffset, p_frgmntFecMng->fragSize );
            }
            else
            {
                // fill the "little" boolean matrix m2
                LoRaFrgmntFecMatrixLineBitSet( missTblIndex, 1, fecMissMatrixLine, 
                                               binaryMatrixSize );
                isMissFound = true;
            }
        }
    }

    if( isMissFound == false )
    {
        return FRGMNT_FEC_STATUS_ONGOING;
    }

    //-----

    missTblIndex      = 0;
    firstMissTblIndex = 0;
    noMoreInfo = false;

    firstMissTblIndex = LoRaFrgmntFecMatrixLineBitSearch( 1, 1, fecMissMatrixLine, binaryMatrixSize );
    bitVal = LoRaFrgmntFecMatrixLineBitGet( firstMissTblIndex, p_frgmntFecMng->tableS, 
                                            binaryMatrixSize );
    while( bitVal == 1 )
    {
        p_dataBlkOffset = 
            FRGMNT_FEC_MACRO_DATABLK_OFFSET_PTR( p_dataBlockBuffer, 
                                                 p_frgmntFecMng->tableMiss[firstMissTblIndex - 1].missedNth, 
                                                 p_frgmntFecMng->fragSize );
        LoRaFrgmntFecXorFrame( fecXorData, p_dataBlkOffset, p_frgmntFecMng->fragSize );

        LoRaFrgmntFecMatrixLineXor( fecMissMatrixLine, 
                                    p_frgmntFecMng->tableMiss[firstMissTblIndex - 1].m2b,
                                    binaryMatrixSize );

        noMoreInfo = LoRaFrgmntFecMatrixLineIsNull( fecMissMatrixLine, binaryMatrixSize );
        if( noMoreInfo == true )
        {
            break;  // exit while() loop
        }

        firstMissTblIndex = LoRaFrgmntFecMatrixLineBitSearch( 1, 1, fecMissMatrixLine, binaryMatrixSize );
        bitVal = LoRaFrgmntFecMatrixLineBitGet( firstMissTblIndex, p_frgmntFecMng->tableS, 
                                                binaryMatrixSize );
    }
    if( noMoreInfo == false )
    {
        memcpy1( p_frgmntFecMng->tableMiss[firstMissTblIndex - 1].m2b, fecMissMatrixLine, binaryMatrixSize );

        p_dataBlkOffset = 
            FRGMNT_FEC_MACRO_DATABLK_OFFSET_PTR( p_dataBlockBuffer, 
                                                 p_frgmntFecMng->tableMiss[firstMissTblIndex - 1].missedNth, 
                                                 p_frgmntFecMng->fragSize );
        memcpy1( p_dataBlkOffset, fecXorData, p_frgmntFecMng->fragSize );

        LoRaFrgmntFecMatrixLineBitSet( firstMissTblIndex, 1, p_frgmntFecMng->tableS, binaryMatrixSize );
        p_frgmntFecMng->m2l++;
    }

    if( p_frgmntFecMng->m2l < p_frgmntFecMng->fragMissingNum )
    {
        return FRGMNT_FEC_STATUS_ONGOING;
    }

    //-----

    if( p_frgmntFecMng->fragMissingNum > 1 )
    {
        for( i = (p_frgmntFecMng->fragMissingNum - 2); i >= 0; i-- )
        {
            for( j = (p_frgmntFecMng->fragMissingNum - 1); j > i; j-- )
            {
                bitVal = LoRaFrgmntFecMatrixLineBitGet( ( j + 1 ), 
                                                        p_frgmntFecMng->tableMiss[ i ].m2b,
                                                        binaryMatrixSize );
                if( bitVal == 1 )
                {
                    memcpy1( fecM2bTmp, p_frgmntFecMng->tableMiss[ i ].m2b, binaryMatrixSize );
                    LoRaFrgmntFecMatrixLineXor( fecM2bTmp, 
                                                p_frgmntFecMng->tableMiss[ j ].m2b,
                                                binaryMatrixSize );

                    memcpy1( p_frgmntFecMng->tableMiss[ i ].m2b, fecM2bTmp, binaryMatrixSize );

                    p_dataBlkOffset = 
                        FRGMNT_FEC_MACRO_DATABLK_OFFSET_PTR( p_dataBlockBuffer, 
                                                             p_frgmntFecMng->tableMiss[j].missedNth, 
                                                             p_frgmntFecMng->fragSize );
                    memcpy1( fecXorData, p_dataBlkOffset, p_frgmntFecMng->fragSize );

                    p_dataBlkOffset = FRGMNT_FEC_MACRO_DATABLK_OFFSET_PTR( p_dataBlockBuffer, 
                                                                           p_frgmntFecMng->tableMiss[i].missedNth, 
                                                                           p_frgmntFecMng->fragSize );
                    LoRaFrgmntFecXorFrame( p_dataBlkOffset, fecXorData, p_frgmntFecMng->fragSize );
                }
            }
        }
    }

    // finished
    LoRaFrgmntReleaseFrgmntFecMng( p_frgmntFecMng );
    return p_frgmntFecMng->fragMissingNum;
}

//-------------------------------------------------------------------------------------------------
// access matrix

static uint8_t LoRaFrgmntFecMatrixLineBitGet( uint16_t index, uint8_t *p_matrix, uint16_t matrixArraySize )
{
    uint8_t     retVal;
    uint16_t    arrayNum;
    uint8_t     bitMask;

    // init
    retVal = 0;

    if( index > 0 )
    {
        arrayNum = (index - 1) / 8;            // 8 = bit number of uint8_t
        bitMask  = 1 << ( (index - 1) % 8 );

        if( arrayNum < matrixArraySize )
        {
            if( ( p_matrix[ arrayNum ] & bitMask ) != 0x00 )
            {
                retVal = 1;
            }
        }
    }

    return( retVal );
}

static void LoRaFrgmntFecMatrixLineBitSet( uint16_t index, 
                                           uint8_t  bitval, 
                                           uint8_t  *p_matrix, 
                                           uint16_t matrixArraySize )
{
    uint16_t    arrayNum;
    uint8_t     bitMask;

    if( index > 0 )
    {
        arrayNum = (index - 1) / 8;            // 8 = bit number of uint8_t
        bitMask  = 1 << ( (index - 1) % 8 );

        if( arrayNum < matrixArraySize )
        {
            if( bitval == 1 )
            {
                p_matrix[ arrayNum ] |= bitMask;
            }
            else
            {
                p_matrix[ arrayNum ] &= ~( bitMask );
            }
        }
    }
}

static uint16_t LoRaFrgmntFecMatrixLineBitSearch( uint16_t startIndex, 
                                                  uint8_t  searchBitval, 
                                                  uint8_t  *p_matrix, 
                                                  uint16_t matrixArraySize )
{
    uint16_t    foundIndex;
    uint16_t    searchIndex;
    uint16_t    i, arrayNum;
    uint8_t     bitMask;

    // init
    foundIndex = (uint16_t)(-1);  // not found

    if( startIndex > 0 )
    {
        arrayNum = (startIndex - 1) / 8;            // 8 = bit number of uint8_t
        bitMask  = 1 << ( (startIndex - 1) % 8 );

        if( arrayNum < matrixArraySize )
        {
            searchIndex = startIndex;
            for( i = arrayNum; i < matrixArraySize; i++ )
            {
                while( bitMask != 0x00 )
                {
                    if( ( ( searchBitval == 0 ) && ( ( p_matrix[i] & bitMask ) == 0x00 ) ) ||
                        ( ( searchBitval == 1 ) && ( ( p_matrix[i] & bitMask ) != 0x00 ) ) )
                    {
                        foundIndex = searchIndex;
                        break;  // exit while looop
                    }

                    searchIndex++;

                    if( bitMask != 0x80 )
                    {
                        bitMask <<= 1;
                    }
                    else
                    {
                        bitMask = 0;
                    }
                }

                if( foundIndex != (uint16_t)(-1) )
                {
                    break;  // found. exit for() loop
                }

                bitMask = 0x01;  // reset for next
            }
        }
    }

    return foundIndex;
}

static void LoRaFrgmntFecMatrixLineXor( uint8_t  *p_matrix1, 
                                        uint8_t  *p_matrix2, 
                                        uint16_t matrixArraySize )
{
    uint16_t    i;
    uint8_t     tmp8;

    for( i = 0; i < matrixArraySize; i++ )
    {
        tmp8 = p_matrix1[i] ^ p_matrix2[i];
        p_matrix1[i] = tmp8;
    }
}

static bool LoRaFrgmntFecMatrixLineIsNull( uint8_t *p_matrix, uint16_t matrixArraySize )
{
    bool        bRet;
    uint16_t    i;

    // init
    bRet = true;

    for( i = 0; i < matrixArraySize; i++ )
    {
        if( p_matrix[i] != 0x00 )
        {
            bRet = false;  // matrix is not null.
            break;
        }
    }

    return bRet;
}

//-------------------------------------------------------------------------------------------------
// Parity matrix

static void LoRaFrgmntFecGetParityMatrixRow( uint16_t fragNth, 
                                             uint16_t M_nbFrag,
                                             uint8_t  *p_matrix, 
                                             uint16_t matrixArraySize )
{
    uint32_t    N_parityMatrixLine;
    uint32_t    m_pow2;
    uint32_t    x_seed;
    uint32_t    r;
    uint32_t    nbCoeff;
    uint16_t    tmpVal16;
    uint16_t    i;

    N_parityMatrixLine = fragNth - M_nbFrag;

    // nbFrag; check it is power of 2
    tmpVal16 = M_nbFrag;
    m_pow2   = 0;
    for( i = 0; i < 16; i++ )
    {
        if( ( tmpVal16 & 0x0001 ) == 1 )
        {
            if( m_pow2 == 0 )
            {
                m_pow2 = 1;
            }
            else
            {
                m_pow2 = 0;  // nbFrag is not power of 2
                break;       // exit for() loop
            }
        }

        tmpVal16 >>= 1;
    }

    // init x
    x_seed = 1 + ( 1001 * N_parityMatrixLine );

    // init parity matrix line
    memset1( p_matrix, 0x00, matrixArraySize );

    // matrix
    nbCoeff = 0;
    while( nbCoeff < ( M_nbFrag >> 1 ) )
    {
        r = (uint32_t)1 << 16;
        while( r >= M_nbFrag )
        {
            x_seed = LoRaFrgmntFecPrbs23( x_seed );
            r = x_seed % ( M_nbFrag + m_pow2 );
        }

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        if( LoRaFrgmntFecMatrixLineBitGet( (r + 1), p_matrix, matrixArraySize ) == 0 )
#endif
        {
            LoRaFrgmntFecMatrixLineBitSet( (r + 1), 1, p_matrix, matrixArraySize );
            nbCoeff += 1;
        }
    }
}

static uint32_t LoRaFrgmntFecPrbs23( uint32_t x_start )
{
    uint32_t    b0, b1;

    b0 = ( x_start & (uint32_t)0x00000001 );
    b1 = ( x_start & (uint32_t)0x00000020 ) >> 5;

    x_start = ( x_start >> 1 ) + ( (uint32_t)(b0 ^ b1) << 22 );

    return x_start;
}

//-------------------------------------------------------------------------------------------------
// Frame

static void LoRaFrgmntFecXorFrame( uint8_t *p_data1, uint8_t *p_data2, uint8_t size )
{
    volatile uint8_t    i, tmp8;

    for( i = 0; i < size; i++ )
    {
        tmp8 = p_data1[i] ^ p_data2[i];
        p_data1[i] = tmp8;
    }
}

//-------------------------------------------------------------------------------------------------
// Mng

static void LoRaFrgmntInitFrgmntFecMng( Frgmnt_FecMng_t *p_frgmntFecMng )
{
    uint8_t     frgIndex;

    if( p_frgmntFecMng != NULL )
    {
        frgIndex = p_frgmntFecMng->fragIndex_fecmng;
        memset1( (uint8_t *)p_frgmntFecMng, 0x00, sizeof(Frgmnt_FecMng_t) );
        p_frgmntFecMng->fragIndex_fecmng = frgIndex;
    }
}

static Frgmnt_FecMng_t* LoRaFrgmntSearchFrgmntFecMng( uint8_t fragIndex )
{
    uint8_t             i;
    Frgmnt_FecMng_t     *p_frgmntFecMng, *p_searchMng;

    // init
    p_frgmntFecMng = NULL;

    for( i = 0; i < FRGMNT_CONFIG_MAX_FRAG_INDEX; i++ )
    {
        p_searchMng = &( FrgmntFecMng[ i ] );

        if( p_searchMng->fragIndex_fecmng == fragIndex )
        {
            p_frgmntFecMng = p_searchMng;
            break;  // exit from for(i) loop
        }
    }

    return p_frgmntFecMng;
}

static Frgmnt_FecMng_t* LoRaFrgmntGetFrgmntFecMng( uint8_t fragIndex )
{
    uint8_t             i;
    Frgmnt_FecMng_t     *p_frgmntFecMng, *p_searchMng;

    // init
    p_frgmntFecMng = NULL;

    // search FecMng
    p_frgmntFecMng = LoRaFrgmntSearchFrgmntFecMng( fragIndex );
    if( p_frgmntFecMng == NULL )
    {
        // search empty FecMng
        for( i = 0; i < FRGMNT_CONFIG_MAX_FRAG_INDEX; i++ )
        {
            p_searchMng = &( FrgmntFecMng[ i ] );

            if( p_searchMng->fragIndex_fecmng == FRGMNT_FEC_FRAGINDEX_NOUSE )
            {
                p_frgmntFecMng = p_searchMng;
                p_frgmntFecMng->fragIndex_fecmng = fragIndex;
            }
        }
    }

    return p_frgmntFecMng;
}

static void LoRaFrgmntReleaseFrgmntFecMng( Frgmnt_FecMng_t *p_frgmntFecMng )
{
    if( p_frgmntFecMng != NULL )
    {
        memset( p_frgmntFecMng, 0x00, sizeof(Frgmnt_FecMng_t) );
        p_frgmntFecMng->fragIndex_fecmng = FRGMNT_FEC_FRAGINDEX_NOUSE;
    }
}
