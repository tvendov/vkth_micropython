/*!
 * \file      timer-board.h
 */
/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __TIMER_BOARD_H__
#define __TIMER_BOARD_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdbool.h>
#include "timer.h"

#define R_RTC_CONST_THETA		(32768UL)
#define RP_IT_CAP_CYCLE_SEC		(16UL)
#if defined(__RA0E1__) || defined(__RA0E2__)
#define RP_IT_CAP_CYCLE_MSEC		(16000UL)
#else // RA2E1, RA2L1
#define RP_IT_CAP_CYCLE_MSEC	(15000UL)
#endif

void BoardTimerInit( void );
void BoardTimerSetAlarm( uint32_t );
void BoardTimerStopAlarm( void );
TimerTime_t BoardTimerGetTimerValue( void );

uint32_t BoardTimerGetCalendarTime( uint16_t *milliseconds );
TimerTime_t BoardTimerComputeElapsedTime( TimerTime_t pastEventInTime );
uint32_t BoardTimerGetAdjustedTimeoutValue( uint32_t timeout );
TimerTime_t BoardTimerGetElapsedAlarmTime ( void );

uint16_t RpMcuCcaGetCurrentCount( void );
uint16_t RpMcuCcaDiffTime( uint16_t countStart );

#define RtcComputeElapsedTime               BoardTimerComputeElapsedTime
#define RtcGetAdjustedTimeoutValue          BoardTimerGetAdjustedTimeoutValue

#define RtcGetElapsedAlarmTime              BoardTimerGetElapsedAlarmTime
#define RtcGetTimerValue                    BoardTimerGetTimerValue
//
#define RtcSetTimeout                       BoardTimerSetAlarm
#define RtcStopTimer                        BoardTimerStopAlarm
//
#define RtcGetCalendarTime                  BoardTimerGetCalendarTime

//! Timer callback type
typedef void ( RtcTimerCallback_t ) ( void );

#ifdef __cplusplus
}
#endif

#endif // __TIMER_BOARD_H__
