/*!
 * \file      timer-board.c
 */
/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#include "board.h"
#include "timer.h"
#include "gpio.h"

#include "timer-board.h"

#include "r_agt.h"

agt_instance_ctrl_t *gp_timer0_ctrl;
agt_instance_ctrl_t *gp_timer1_ctrl;
timer_cfg_t *gp_timer0_cfg;
timer_cfg_t *gp_timer1_cfg;

//! Timer operation states
typedef enum {
    RP_MCU_TIMER_STATE_NULL = 0,           //!< Timer is not operating
    RP_MCU_TIMER_STATE_INIT,               //!< Timer operation triggered
    RP_MCU_TIMER_STATE_RTC_SYNC,           //!< Synchronizing to interval interrupt by RTC
    RP_MCU_TIMER_STATE_SEC_COUNT,          //!< Count seconds by RTC
    RP_MCU_TIMER_STATE_SUB_SEC_COUNT_SET,  //!< Count remaining sub seconds (Timer Setting)
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
bool RpMcuTimerEventControl(RpMcuTimerEvent_t eventType, uint32_t timeout);
bool RpMcuTimerRun(RpMcuTimerEvent_t eventType, uint32_t timeout);
static uint32_t RpMcuCalendarMillisecondGet(void);
uint32_t RpMcuCounterToMilliSec(uint16_t count);

void RpMcuResourceTimerStart( void );
void RpMcuResourceTimerStop( void );

uint16_t RpMcuFreeRunTimerValue( void );

static void RpMcuCompareTimerSet( uint16_t intervalInMs );
static void RpMcuCompareTimerStart( void );
uint16_t RpMcuCompareTimerValue( void );

bool RpMcuTimerCallCallback( void );

void BoardTimerInit( void )
{
	gp_timer0_ctrl = (agt_instance_ctrl_t *)&g_timer0_ctrl;
	gp_timer1_ctrl = (agt_instance_ctrl_t *)&g_timer1_ctrl;
	gp_timer0_cfg = (timer_cfg_t *)&g_timer0_cfg;
	gp_timer1_cfg = (timer_cfg_t *)&g_timer1_cfg;

    if (FSP_SUCCESS != R_AGT_Open(gp_timer0_ctrl, gp_timer0_cfg)) {
        return;
    }

    if (FSP_SUCCESS != R_AGT_Open(gp_timer1_ctrl, gp_timer1_cfg)) {
        R_AGT_Close(gp_timer0_ctrl);
    }

	RpMcuTimerMng.irqLocked = true;
	{
		RpMcuTimerMng.pCallback = TimerIrqHandler;
    }
	RpMcuTimerMng.irqLocked = false;

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

TimerTime_t RpMcuTimerGetTime()
{
	uint8_t intFlag;
	uint16_t subSecCount;
	uint32_t milliseconds;
	uint32_t secondsTimeTemp;
    TimerTime_t diffInMs=0;                  // TimerTime_t being uint64_t

	CRITICAL_SECTION_BEGIN();

	secondsTimeTemp = secondsTime;
	subSecCount = RpMcuFreeRunTimerValue();	// < 16sec
	intFlag = gp_timer0_ctrl->p_reg->AGT16.CTRL.AGTCR_b.TUNDF;

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
 * \brief Stops timer
 */
void RpMcuTimerStop( void )
{
    RpMcuTimerMng.irqLocked = true;   // Equivalent to RpMcuTimerMacroIrqLock()
    RpMcuTimerMng.state     = RP_MCU_TIMER_STATE_INIT;
    RpMcuTimerMng.tsStart   = 0ULL;
    RpMcuTimerMng.timeRem   = 0UL;
    RpMcuTimerMng.rtcSyncTimeRem = 0U;
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
            RpMcuTimerRun(eventType, timeout);								// Return value doesn't have to be checked as the state machine is initialized
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
	uint16_t msVal;
    uint16_t timeRemMs = 0;

	switch(eventType)
	{
		case RP_MCU_TIMER_EVENT_START:
			if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_INIT )
			{
				rslt = true;
				msVal = (uint16_t)RpMcuCalendarMillisecondGet();
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

 			if ( RpMcuTimerMng.timeRem > 0xFFFF0000 )	// fail safe when the remaining time underflows
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
			if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_SUB_SEC_COUNT_END )
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
		RpMcuTimerMng.state = RP_MCU_TIMER_STATE_END;

		RpMcuTimerCallCallback();

		if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_END )
		{
			RpMcuTimerMng.irqLocked = true;
			RpMcuTimerMng.state = RP_MCU_TIMER_STATE_INIT;
		}
	}
	else
	{
		if ( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_SUB_SEC_COUNT_SET )
		{
			RpMcuCompareTimerSet( timeRemMs );
			RpMcuTimerMng.state = RP_MCU_TIMER_STATE_SUB_SEC_COUNT_END;
			RpMcuCompareTimerStart();
		}
	}

	return rslt;
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
//@		DPRINT("\n*ERR CB_LOCKED\n");
    }

    return !callbackLocked;
}

void RpMcuResourceTimerStart( void )
{
	// AGT0
	R_AGT_Start(gp_timer0_ctrl);

	// AGT1
	R_BSP_IrqCfgEnable(VECTOR_NUMBER_AGT1_COMPARE_A, gp_timer1_cfg->cycle_end_ipl, gp_timer1_ctrl);
	gp_timer1_ctrl->p_reg->AGT16.CTRL.AGTCMSR |= 0x01;		// Compare A enable
	R_AGT_Start(gp_timer1_ctrl);
}
void RpMcuResourceTimerStop( void )
{
	R_AGT_Stop(gp_timer0_ctrl);
	R_AGT_Stop(gp_timer1_ctrl);
	while(gp_timer0_ctrl->p_reg->AGT16.CTRL.AGTCR_b.TCSTF);
	while(gp_timer1_ctrl->p_reg->AGT16.CTRL.AGTCR_b.TCSTF);
}

uint16_t RpMcuFreeRunTimerValue( void )
{
	uint16_t regVal;

	regVal = gp_timer0_ctrl->p_reg->AGT16.AGT;

	return (0xFFFF - regVal);
}

static void RpMcuCompareTimerSet( uint16_t intervalInMs )
{
	uint16_t nowCountPre;
	uint16_t nowCountPost;
	uint16_t setCount;
    uint32_t temp;

	nowCountPre = gp_timer1_ctrl->p_reg->AGT16.AGT;
	R_AGT_Stop(gp_timer1_ctrl);

	if ( intervalInMs != RP_IT_CAP_CYCLE_MSEC )
	{
		// Calculate and set interval counts for counter clock frequency of 32.768 kHz
		temp = (uint32_t)(R_RTC_CONST_THETA / 8 ) * (uint32_t)intervalInMs;
		temp = temp / 1000;
	}
	else
	{
		temp = 0xEFFF;
	}

	// Compare Time (CurrentTime - RemainingTime)
	setCount = nowCountPre - (uint16_t)temp + 3;	// 3 count(Delay by start)

	if ( nowCountPre < setCount )
	{
		setCount -= 0x1000;
	}

	while(gp_timer1_ctrl->p_reg->AGT16.CTRL.AGTCR_b.TCSTF);

	nowCountPost = gp_timer1_ctrl->p_reg->AGT16.AGT;
#if 1
	if ( nowCountPre >= nowCountPost )
	{
		if ( setCount >= nowCountPost )
		{
			setCount = nowCountPost;
		}
	}
	else // if ( nowCountPre < nowCountPost )
	{
		if (( nowCountPre > setCount ) || (setCount >= nowCountPost))
		{
			setCount = nowCountPost;
		}
	}
#endif
	gp_timer1_ctrl->p_reg->AGT16.AGTCMA = setCount;
}

static void RpMcuCompareTimerStart( void )
{
    R_BSP_IrqStatusClear(VECTOR_NUMBER_AGT1_COMPARE_A);
	R_AGT_Start(gp_timer1_ctrl);
}

uint16_t RpMcuCompareTimerValue( void )
{
	uint16_t regVal;

	regVal = gp_timer1_ctrl->p_reg->AGT16.AGT;

	return (0xEFFF - regVal);
}

/////////////////////////////////////////////////////
///////////////   INTERRUPT HANDLER    //////////////
/////////////////////////////////////////////////////

void RpMcuFreeRunTimerIntHandler(timer_callback_args_t *p_args)
{
	uint8_t wk;

	wk = gp_timer0_ctrl->p_reg->AGT16.CTRL.AGTCR;

	if ( wk & 0x20 )
	{
	 	// Current Seconds Time
		secondsTime += RP_IT_CAP_CYCLE_SEC;
	}
}

void RpMcuCompareTimerIntHandler(timer_callback_args_t *p_args)
{
	uint8_t wk;

	wk = gp_timer0_ctrl->p_reg->AGT16.CTRL.AGTCR;

	if ( wk & 0x20 )
	{
		gp_timer0_ctrl->p_reg->AGT16.CTRL.AGTCR = wk & ~(0x20);

	 	// Current Seconds Time (for sleep)
		secondsTime += RP_IT_CAP_CYCLE_SEC;
	}

	// Trigger INTRTC timer event if timer is in operation
	if(RpMcuTimerIsTimerOperating())
	{
		RpMcuTimerEventControl(RP_MCU_TIMER_EVENT_INT_RTC, 0);
	}
}

static uint32_t RpMcuCalendarMillisecondGet(void)
{
    uint16_t msVal;
    uint16_t SubSecCount;

	SubSecCount = RpMcuCompareTimerValue();

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
	volatile uint32_t time;				// return value: count converted to millisecond

	time = (uint32_t)count * 24414;	// 8/32.768E3/1E8
	time /= 100000UL;

    return time;
}

#define RP_MAX_SYSTEMTIME                  (0xffffffffffffffffU) //!< Maximum system time that can be handled by the RTC alarm counter without overflow (64-bit system time assumed)

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

uint32_t BoardTimerGetCalendarTime( uint16_t *milliseconds )
{
    // Time should be based on GPS epoch (starting at 1980 jan 6 at midnight)
    // Currently calculated time is based on the initialization time

    TimerTime_t mcuTime = RpMcuTimerGetTime();

    *milliseconds = mcuTime % 1000UL; // subsecond

    return( mcuTime / 1000UL );  // second
}

uint16_t RpMcuCcaGetCurrentCount( void )
{
    return RpMcuFreeRunTimerValue();
}

uint16_t RpMcuCcaDiffTime( uint16_t countStart )
{
    uint16_t countCurrent;
	uint16_t countDiff;
    volatile uint32_t timeDiff = 0;

    countCurrent = RpMcuFreeRunTimerValue();
    countDiff = countCurrent - countStart;

	if( 1 < countDiff )
	{
		timeDiff = (countDiff - 1) * 244U;	// Convert	[count] to [us]. 244 is 8/32.768E3 in [us]
	}

    return timeDiff;
}

void agt_comp_int_isr (void)
{
    IRQn_Type irq = R_FSP_CurrentIrqGet();

    /* Clear pending IRQ to make sure it doesn't fire again after exiting */
    R_BSP_IrqStatusClear(irq);

    /* Recover ISR context saved in open. */
    agt_instance_ctrl_t * p_instance_ctrl = (agt_instance_ctrl_t *) R_FSP_IsrContextGet(irq);

    /* Save AGTCR to determine the source of the interrupt. */
    uint32_t agtcr = p_instance_ctrl->p_reg->AGT16.CTRL.AGTCR;

	if( RpMcuTimerMng.state == RP_MCU_TIMER_STATE_SUB_SEC_COUNT_END )
	{
		RpMcuTimerEventControl(RP_MCU_TIMER_EVENT_INT_IT, 0);
	}

	/* Clear flags in AGTCR.TCMAF */
	p_instance_ctrl->p_reg->AGT16.CTRL.AGTCR = (uint8_t) (agtcr & ~0x40);
}
