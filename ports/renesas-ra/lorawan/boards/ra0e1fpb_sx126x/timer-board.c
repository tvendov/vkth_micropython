/*!
 * \file      timer-board.c
 */
/*
    Copyright (c) 2024 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#include "board.h"
#include "timer.h"
#include "gpio.h"

#include "timer-board.h"

#define RP_MAX_SYSTEMTIME                  (0xffffffffffffffffU) //!< Maximum system time that can be handled by the RTC alarm counter without overflow (64-bit system time assumed)

tml_instance_ctrl_t *gp_timer0_ctrl;
timer_cfg_t *gp_timer0_cfg;
tau_instance_ctrl_t *gp_timer1_ctrl;
timer_cfg_t *gp_timer1_cfg;

//! Timer operation states
typedef enum {
    RP_MCU_TIMER_STATE_NULL = 0,           //!< Timer is not operating
    RP_MCU_TIMER_STATE_INIT,               //!< Timer operation triggered
    RP_MCU_TIMER_STATE_RTC_SYNC,           //!< Synchronizing to interval interrupt by RTC
    RP_MCU_TIMER_STATE_SEC_COUNT,          //!< Count seconds by RTC
    RP_MCU_TIMER_STATE_SUB_SEC_COUNT_SET,  //!< Count remaining sub seconds (Timer Setting)
	RP_MCU_TIMER_STATE_SUB_SEC_COUNT_CHECK,//!< Count remaining sub seconds (Timer Check State)
	RP_MCU_TIMER_STATE_SUB_SEC_COUNT_END,  //!< Count remaining sub seconds (Timer End Process)
    RP_MCU_TIMER_STATE_END,                //!< Timeout count finished and callback is to be called
} RpMcuTimerState_t;

//! Timer operation management structure
typedef struct RpMcuTimerMng_s{
    RpMcuTimerState_t   state;         //!< Current timer state
    TimerTime_t         tsStart;       //!< Start timestamp of the current timer
    uint32_t            timeRem;       //!< Remaining time until timeout
    bool                irqLocked;     //!< Timer IRQ callback lock state
    RtcTimerCallback_t *pCallback;     //!< Timer IRQ callback pointer
	uint16_t			rtcSyncTimeRem;//!< Remaining time until RTC sync
}RpMcuTimerMng_t;

//! Enumeration of timer operation events
typedef enum{
    RP_MCU_TIMER_EVENT_INIT = 0,           //!< Timer initialization request
    RP_MCU_TIMER_EVENT_START,              //!< Timer start request
    RP_MCU_TIMER_EVENT_INT_RTC,            //!< INTRTC interrupt
    RP_MCU_TIMER_EVENT_INT_IT,             //!< INTIT interrupt
    RP_MCU_TIMER_EVENT_FORCE_STOP,         //!< Timer stop request
} RpMcuTimerEvent_t;

//! Timer operation management variable default values
static const RpMcuTimerMng_t RpMcuTimerMngDefault = {
    .state     = RP_MCU_TIMER_STATE_NULL,
    .tsStart   = 0ULL,
    .timeRem   = 0UL,
    .irqLocked = true,
    .pCallback = NULL,
    .rtcSyncTimeRem = 0UL,
};

static uint32_t secondsTime = 0;									//!< Time elapsed since startup [sec]

//// Static global variable
static RpMcuTimerMng_t RpMcuTimerMng;								//!< Timer operation management variable

TimerTime_t RpMcuTimerGetTime( void );
bool RpMcuTimerEventControl( RpMcuTimerEvent_t eventType, uint32_t timeout );
bool RpMcuTimerRun( RpMcuTimerEvent_t eventType, uint32_t timeout );
void RpMcuTimerStop( void );
bool RpMcuTimerIsTimerOperating( void );
bool RpMcuTimerCallCallback( void );

void RpMcuResourceTimerInit( void );
void RpMcuResourceTimerStart( void );
void RpMcuResourceTimerStop( void );

static uint32_t RpMcuCalendarMillisecondGet( void) ;
uint32_t RpMcuCounterToMilliSec(uint16_t count);

static void RpMcu32bitIntervalTimerInit( void );
static void RpMcu32bitIntervalTimerStart( void );
static void RpMcu32bitIntervalTimerStop( void );
static void RpMcu16bitIntervalTimerStart( void );
static void RpMcu16bitIntervalTimerStop( void );
static void RpMcu16bitIntervalTimerSet( uint16_t intervalInMs );
uint16_t RpMcu16bitCaptureTimerValue( void );

void RpMcuTau02Init( void );
void RpMcuTau02Start( void );
void RpMcuTau02Stop( void );
uint16_t RpMcuTau02Value( void );


void BoardTimerInit( void )
{
    RpMcuTimerMng.irqLocked = true;
    {
        RpMcuTimerMng.pCallback = TimerIrqHandler;
    }
    RpMcuTimerMng.irqLocked = false;

    RpMcuResourceTimerInit();
    RpMcuResourceTimerStart();
}

void BoardTimerSetAlarm( uint32_t timeout )
{
    RpMcuTimerEventControl( RP_MCU_TIMER_EVENT_START, timeout );
}

void BoardTimerStopAlarm( void )
{
    RpMcuTimerEventControl( RP_MCU_TIMER_EVENT_FORCE_STOP, 0 );
}

TimerTime_t BoardTimerGetTimerValue( void )
{
    return RpMcuTimerGetTime();
}

uint32_t BoardTimerGetCalendarTime( uint16_t *milliseconds )
{
    // Time should be based on GPS epoch (starting at 1980 jan 6 at midnight)
    // Currently calculated time is based on the initialization time

    TimerTime_t mcuTime = RpMcuTimerGetTime();

    *milliseconds = mcuTime % 1000UL; // subsecond

    return( mcuTime / 1000UL );  // second
}

TimerTime_t BoardTimerComputeElapsedTime( TimerTime_t pastEventInTime )
{
    TimerTime_t currentSystime, ret;

    currentSystime = RpMcuTimerGetTime();

    if( currentSystime < pastEventInTime )
    { // 64-bit system time counter is large enough and this should not happen.
        ret = 0xFFFFFFFFFFFFFFFFULL; // return maximum value as error
    }
    else
    {
        ret = currentSystime - pastEventInTime ;
    }

    return ret;
}

uint32_t BoardTimerGetAdjustedTimeoutValue( uint32_t timeout )
{
    return  timeout;
}

TimerTime_t BoardTimerGetElapsedAlarmTime ( void )
{
    TimerTime_t currentTime, ret;

    currentTime = RpMcuTimerGetTime( );

    if( currentTime < RpMcuTimerMng.tsStart )
    {
        ret = currentTime + ( RP_MAX_SYSTEMTIME - RpMcuTimerMng.tsStart );
    }
    else
    {
        ret = currentTime - RpMcuTimerMng.tsStart;
    }

    return ret;
}

uint16_t RpMcuCcaGetCurrentCount( void )
{
    return RpMcuTau02Value();
}

uint16_t RpMcuCcaDiffTime( uint16_t countStart )
{
    uint16_t countCurrent;
    uint16_t countDiff;
    volatile uint32_t timeDiff = 0;

    countCurrent = RpMcuTau02Value();
    countDiff = countStart - countCurrent;  // Down Count

    timeDiff = ( (uint32_t) countDiff ) * 30U;  // Convert [count] to [usec]. 1/32768[Hz] * 1000000[usec/count].

    return (uint16_t) timeDiff;
}

TimerTime_t RpMcuTimerGetTime()
{
    uint8_t intFlag;
    uint16_t subSecCount;
    uint32_t milliseconds;
    uint32_t secondsTimeTemp;
    TimerTime_t diffInMs=0;                  // TimerTime_t being uint64_t

    CRITICAL_SECTION_BEGIN();

    secondsTimeTemp = secondsTime;
    subSecCount = RpMcu16bitCaptureTimerValue();    // < 16sec
    intFlag = R_TML->ITLS0;

    CRITICAL_SECTION_END();

    if ( subSecCount < 0x7FFF )
    {
        if ( intFlag & 0x01 )
        {
            secondsTimeTemp += RP_IT_CAP_CYCLE_SEC;
        }
    }

    milliseconds = RpMcuCounterToMilliSec( subSecCount );

    diffInMs = ((TimerTime_t)secondsTimeTemp * 1000UL) + milliseconds;

    return diffInMs;

}

/*!
 * \brief                    Timer event management.
 * \param[in] eventType      Type of occured event
 * \param[in] timeout        Timer timeout in millisecond.
 *                           Applied if eventType is RP_MCU_TIMER_EVENT_START. Set 0 for other eventType's.
 * \return                   Transition result
 *            \retval true   Success
 *            \retval false  Fail
 */
bool RpMcuTimerEventControl(RpMcuTimerEvent_t eventType, uint32_t timeout)
{
    bool rslt = false;

    switch(eventType)
    {
        case RP_MCU_TIMER_EVENT_START:
            RpMcuTimerStop();
            RpMcuTimerRun(eventType, timeout);                              // Return value doesn't have to be checked as the state machine is initialized
            break;
        case RP_MCU_TIMER_EVENT_INT_RTC:
        case RP_MCU_TIMER_EVENT_INT_IT:
            if(RpMcuTimerRun(eventType, 0UL) == true){
                // Something to do?
                rslt = true;
            }else{ // Erroneous case (e.g. illegal state)
                RpMcuTimerStop(); // Stop timer operation due to illegal behavior
                rslt = false;     // Clarification
            }
            break;
        case RP_MCU_TIMER_EVENT_FORCE_STOP:
            RpMcuTimerStop();     // Stop current timer
            break;
        case RP_MCU_TIMER_EVENT_INIT:
        default:
            RpMcuTimerMng = RpMcuTimerMngDefault;    // Initialize everything including callback pointer
            break;
    }

    return rslt;
}


/*!
 * \brief                    Timer operation (state machine) management.
 * \param[in] elapsedTime    Time elapsed since last
 * \param[in] end            End timer operation
 * \return                   Transition result
 *            \retval true   Success
 *            \retval false  Fail
 */
bool RpMcuTimerRun(RpMcuTimerEvent_t eventType, uint32_t timeout)
{
    bool rslt = false;
    uint16_t msVal = (uint16_t)RpMcuCalendarMillisecondGet();
    uint16_t timeRemMs = 0;

    switch(eventType)
    {
        case RP_MCU_TIMER_EVENT_START:
            if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_INIT )
            {
                rslt = true;
                RpMcuTimerMng.irqLocked = false;
                RpMcuTimerMng.tsStart = RpMcuTimerGetTime();
                RpMcuTimerMng.timeRem = timeout;
                RpMcuTimerMng.rtcSyncTimeRem = RP_IT_CAP_CYCLE_MSEC - msVal;

                if ( RpMcuTimerMng.timeRem == 0UL )
                {
                    // No operation required
                }
                else if ( RpMcuTimerMng.timeRem < RP_IT_CAP_CYCLE_MSEC )
                {
                    // Note: to consider whether to introduce margin for improvement
                    if ( RpMcuTimerMng.rtcSyncTimeRem > RpMcuTimerMng.timeRem)
                    {
                        // RpMcuCalendarMillisecondGet() does not return greater than (RP_IT_CAP_CYCLE_MSEC)
                        RpMcuTimerMng.state = RP_MCU_TIMER_STATE_SUB_SEC_COUNT_SET;
                        timeRemMs = RpMcuTimerMng.timeRem;
                        RpMcuTimerMng.timeRem += msVal;
                    }
                    else
                    {
                        RpMcuTimerMng.state = RP_MCU_TIMER_STATE_RTC_SYNC;
                        // Should check if interval RTC interrupt is enabled?
                    }
                }
                else
                { // timeout >= 16000 [ms]
                    RpMcuTimerMng.state = RP_MCU_TIMER_STATE_RTC_SYNC;
                }
            }
            else
            {
                // Error: Timer should be triggered with all operational information initialized
                rslt = false; // Just for clarification
            }
            break;
        case RP_MCU_TIMER_EVENT_INT_RTC:
            if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_RTC_SYNC )
            {
                RpMcuTimerMng.timeRem -= RpMcuTimerMng.rtcSyncTimeRem;
                rslt = true;
            }
            else if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_SEC_COUNT )
            {
                RpMcuTimerMng.timeRem -= RP_IT_CAP_CYCLE_MSEC;
                rslt = true;
            }
            else if (( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_SUB_SEC_COUNT_SET ) ||
                ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_SUB_SEC_COUNT_CHECK ) ||
                ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_SUB_SEC_COUNT_END ))
            {
                // Warning: RTC interval interrrupts are ignored when counting the last remaning sub seconds.
                // Note: to consider whether to check time for fail safe
                RpMcuTimerMng.timeRem = 0UL;
                rslt = true;
            }
            else
            {
                // Note: to consider whether to make action on INIT for fail safe
            }

            if ( RpMcuTimerMng.timeRem > 0xFFFF0000 )   // fail safe when the remaining time underflows
            {
                RpMcuTimerMng.timeRem = 0UL;
            }

            if ( RpMcuTimerMng.timeRem < RP_IT_CAP_CYCLE_MSEC )
            {
                // RpMcuTimerMng.timeRem == 0 is taken care of later
                RpMcuTimerMng.state = RP_MCU_TIMER_STATE_SUB_SEC_COUNT_SET;
                timeRemMs = RpMcuTimerMng.timeRem;
            }
            else
            {
                RpMcuTimerMng.state = RP_MCU_TIMER_STATE_SEC_COUNT;
            }
            break;
        case RP_MCU_TIMER_EVENT_INT_IT:
            if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_SUB_SEC_COUNT_CHECK )
            {
                if ( RpMcuTimerMng.timeRem > msVal )
                {
                    timeRemMs = RpMcuTimerMng.timeRem - msVal;

                    if ( timeRemMs <= 2000UL )
                    {
                        RpMcuTimerMng.state = RP_MCU_TIMER_STATE_SUB_SEC_COUNT_SET;
                    }
                }
                else //if ( RpMcuTimerMng.timeRem <= msVal )
                {
                    RpMcuTimerMng.timeRem = 0UL;
                }
                rslt = true;
            }
            else if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_SUB_SEC_COUNT_END )
            {
                RpMcuTimerMng.timeRem = 0UL;
                rslt = true;
            }
            else
            {
                // Error: 16-bit interval timer runs only to count the last remaining sub seconds.
                rslt = false;
            }
            break;
        default:
            rslt = false;
            break;
    }

    if ( RpMcuTimerMng.timeRem == 0UL )
    {
        RpMcu16bitIntervalTimerStop();
        RpMcuTimerMng.state = RP_MCU_TIMER_STATE_END;
    }
    else
    {
        if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_SUB_SEC_COUNT_SET )
        {
            RpMcu16bitIntervalTimerStop();

            if ( timeRemMs > 2000U )
            {
                timeRemMs = 2000U;
                RpMcuTimerMng.state = RP_MCU_TIMER_STATE_SUB_SEC_COUNT_CHECK;
            }
            else
            {
                RpMcuTimerMng.state = RP_MCU_TIMER_STATE_SUB_SEC_COUNT_END;
            }

            RpMcu16bitIntervalTimerSet( timeRemMs );
            RpMcu16bitIntervalTimerStart();
        }
    }

    if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_END )
    {
        RpMcuTimerCallCallback();

        if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_END )
        {
            RpMcuTimerMng.irqLocked = true;
            RpMcuTimerMng.state = RP_MCU_TIMER_STATE_INIT;
        }
    }

    return rslt;
}

/*!
 * \brief Stops timer
 */
void RpMcuTimerStop( void )
{
    RpMcuTimerMng.irqLocked = true;   // Equivalent to RpMcuTimerMacroIrqLock()
    RpMcu16bitIntervalTimerStop();
    RpMcuTimerMng.state     = RP_MCU_TIMER_STATE_INIT;
    RpMcuTimerMng.tsStart   = 0ULL;
    RpMcuTimerMng.timeRem   = 0UL;
    RpMcuTimerMng.rtcSyncTimeRem = 0U;
}

//! Checks if timer is currently in operation
bool RpMcuTimerIsTimerOperating( void )
{
    bool              rslt = false;
    RpMcuTimerState_t state;

    state = RpMcuTimerMng.state;

    switch(state)
    {
        case RP_MCU_TIMER_STATE_INIT:
        case RP_MCU_TIMER_STATE_NULL:
            rslt = false;
            break;
        default:
            rslt = true;
            break;
    }

    return rslt;
}

/*!
 * \brief            Calls Timer IRQ callback if IRQ is not locked
 * \return           Timer callback lock state.
 *    \retval  true  Successfully called the timer callback
 *    \retval  false Could not call the timer callback due to its lock state
 */
bool RpMcuTimerCallCallback( void )
{
    bool callbackLocked = RpMcuTimerMng.irqLocked;

    if(!callbackLocked)
    {
        if(RpMcuTimerMng.pCallback != NULL)
        {
            RpMcuTimerMng.pCallback();
        }
        SetLowPowerFlag(WAKEUP_TRIGGER_TIMER_TIMEOUT);
    }
    else
    {
//@     DPRINT("\n*ERR CB_LOCKED\n");
    }

    return !callbackLocked;
}

void RpMcuResourceTimerInit( void )
{
    RpMcu32bitIntervalTimerInit();
    RpMcuTau02Init();
}
void RpMcuResourceTimerStart( void )
{
    RpMcu32bitIntervalTimerStart();
    RpMcuTau02Start();
}
void RpMcuResourceTimerStop( void )
{
    RpMcu32bitIntervalTimerStop();
    RpMcuTau02Stop();
}

static uint32_t RpMcuCalendarMillisecondGet(void)
{
    uint16_t msVal;
    uint16_t SubSecCount;

    SubSecCount = RpMcu16bitCaptureTimerValue();
    msVal = RpMcuCounterToMilliSec( SubSecCount );

    return  (uint32_t)msVal;
}

/*!
 *\brief            Convert fSUB clock counts to millisecond
 *\param[in] count  fsxl (32.768 kHz) count
 *\return           Millisecond value
 */
uint32_t RpMcuCounterToMilliSec(uint16_t count)
{
    volatile uint32_t time;             // return value: count converted to millisecond

    time = (uint32_t)count * 24414UL;   // 8/32.768E3/1E8
    time /= 100000UL;

    return time;
}

static void RpMcu32bitIntervalTimerInit( void )
{
    gp_timer0_ctrl = (tml_instance_ctrl_t *)&g_timer0_ctrl;
    gp_timer0_cfg = (timer_cfg_t *)&g_timer0_cfg;

    if (FSP_SUCCESS != R_TML_Open(gp_timer0_ctrl, gp_timer0_cfg)) {
        return;
    }

    R_TML->ITLCSEL0 = 0x44;     // Interval timer count source

    R_TML->ITLCMP00 = 0xFFFF;     // 16sec cycle
    R_TML->ITLMKF0 = 0x1A;        // Interval timer match detection mask
    R_TML->ITLCC0 = 0x82;         // Captuer Enable, fsxl trigger

    R_TML->ITLCC0_b.CAPFCR = 1;   // Clear CAPF0 Capture flag
    R_TML->ITLS0 = 0x1C;            // Clear Interval Timer Status Flag
}

static void RpMcu32bitIntervalTimerStart( void )
{
    R_TML_Start(gp_timer0_ctrl);

    R_TML->ITLCTL0_b.EN0 = 1;   // Count Enable
    R_TML->ITLCTL0_b.EN2 = 1;   // Count Enable
}

static void RpMcu32bitIntervalTimerStop( void )
{
    R_TML->ITLCTL0_b.EN0 = 0;   // Count Enable
    R_TML->ITLCTL0_b.EN2 = 0;   // Count Enable

    R_TML_Stop(gp_timer0_ctrl);
}

static void RpMcu16bitIntervalTimerStart( void )
{
    R_TML->ITLS0 = 0x1B;    // Clear Interval Timer Status Flag

    R_TML->ITLCTL0_b.EN2 = 1;   // Count Enable
}

static void RpMcu16bitIntervalTimerStop( void )
{
    R_TML->ITLCTL0_b.EN2 = 0;   // Count Enable
}

static void RpMcu16bitIntervalTimerSet( uint16_t intervalInMs )
{
    uint32_t temp;

    if ( intervalInMs != 2000 )
    {
        // Calculate and set interval counts for counter clock frequency of 32.768 kHz
        temp = (uint32_t)(R_RTC_CONST_THETA) * (uint32_t)intervalInMs;
        temp = (temp / 100) + 5;    // Round off
        R_TML->ITLCMP01 = (uint16_t)(temp / 10);
    }
    else
    {
        R_TML->ITLCMP01 = 0xFFFF;
    }
}

uint16_t RpMcu16bitCaptureTimerValue( void )
{
    uint16_t regVal;

    do
    {
        regVal = R_TML->ITLCAP00;
    }while(regVal != R_TML->ITLCAP00);

    return regVal;
}

void RpMcuTau02Init( void )
{
    gp_timer1_ctrl = (tau_instance_ctrl_t *)&g_timer1_ctrl;
    gp_timer1_cfg = (timer_cfg_t *)&g_timer1_cfg;

    if (FSP_SUCCESS != R_TAU_Open(gp_timer1_ctrl, gp_timer1_cfg)) {
        return;
    }
}

void RpMcuTau02Start( void )
{
    R_TAU_Start(gp_timer1_ctrl);
}

void RpMcuTau02Stop( void )
{
    R_TAU_Stop(gp_timer1_ctrl);
}

uint16_t RpMcuTau02Value( void )
{
    return (uint16_t)R_TAU->TCR0[2];
}

/////////////////////////////////////////////////////
///////////////   INTERRUPT HANDLER    //////////////
/////////////////////////////////////////////////////

void RpMcu32bitCaptureIntHandler( void )
{
    if ( R_TML->ITLS0 & 0x04 )
    {
        R_TML->ITLS0 = 0x1B;    // Clear Interval Timer Status Flag

        RpMcuTimerEventControl(RP_MCU_TIMER_EVENT_INT_IT, 0);
    }

    if ( R_TML->ITLS0 & 0x01 )
    {
        R_TML->ITLS0 = 0x1E;    // Clear Interval Timer Status Flag

        // Current Seconds Time
        while( RpMcu16bitCaptureTimerValue() > 0xFFF0 );
        secondsTime += RP_IT_CAP_CYCLE_SEC;

        // Trigger INTRTC timer event if timer is in operation
        if(RpMcuTimerIsTimerOperating())
        {
            RpMcuTimerEventControl(RP_MCU_TIMER_EVENT_INT_RTC, 0);
        }
    }
}
