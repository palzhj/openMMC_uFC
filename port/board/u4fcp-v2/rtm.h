#ifndef RTM_H_
#define RTM_H_

#include <stdbool.h>

/**
 * @brief Start the RTM manager (board bring-up, before the scheduler).
 *
 * The manager owns everything that touches Zone 3: the 12 V switch, the FRU
 * identification, the SDR records of the uRTM FMC monitors, the handle and
 * the fault monitor.  It is the only task that talks to the RTM bus, so the
 * payload power state machine never blocks on an RTM transfer.
 */
void rtm_init(void);

/**
 * @brief Tell the manager whether the AMC payload rails are up.
 *
 * The RTM is powered from the AMC, so its 12 V is only switched on while the
 * payload rails are up (the UCD that owns the switch answers only then).
 * Called by the payload state machine on power-up and on shutdown.
 */
void rtm_payload_up(bool up);

/**
 * @brief Is a module plugged into Zone 3?  (RTM_PS#, PA8, low = plugged)
 *
 * This is a plain GPIO read: it says nothing about what the module is.
 */
bool rtm_present(void);

/**
 * @brief Is the RTM 12 V rail switched on by the manager?
 */
bool rtm_12v_is_on(void);

#endif
