#ifndef RTM_FRU_H_
#define RTM_FRU_H_

#include <stdbool.h>

/** Result of the RTM identification (see rtm_fru_type()). */
typedef enum {
    RTM_FRU_UNKNOWN = 0,   /**< no FRU, blank FRU, or no answer (yet) */
    RTM_FRU_URTM_V23,      /**< uRTM v2.3 (the UCD90124A monitor board) */
    RTM_FRU_OTHER,         /**< a FRU answered but it is another board */
} rtm_fru_type_t;

/**
 * @brief Identify the module plugged into Zone 3 from its FRU EEPROM.
 *
 * The AMC publishes the uRTM v2.3 FMC2/FMC3 monitor sensors, which only exist
 * on that board (UCD90124A at 0x5B).  Another RTM - e.g. the RTM Carrier for
 * LattePanda Mu, which carries its own MMC and no UCD - must not get those
 * SDR records, and a blank / unreadable FRU means the AMC has no evidence
 * that the RTM wants to be managed at all.
 *
 * The answer comes from the RTM's FRU at I2C 0x55: the product-area fields
 * must say "uRTM" and version "2.3".  The check is cached; it is dropped when
 * a module is plugged in (RTM_PS# falling edge) or on an explicit
 * rtm_fru_invalidate().
 *
 * The EEPROM is read twice over the RTM path: first through the uRTM's own
 * switch (0x70 ch5 -> 0x71 ch7, where the uRTM keeps its FRU), then directly
 * on the RTM bus (0x70 ch5 -> 0x55) for boards without that switch.  The
 * uRTM FRU only answers once its 12 V is on, so a "no" answer is re-probed
 * every few seconds instead of being final.
 *
 * @retval true the RTM answered and its FRU identifies it as uRTM v2.3
 */
bool rtm_fru_is_urtm_v23(void);

/**
 * @brief What the RTM's FRU says it is.
 *
 * RTM_FRU_UNKNOWN means "no answer" (no RTM, blank EEPROM, or the uRTM whose
 * FRU needs the 12 V that is not on yet), so the caller must stay
 * conservative: no RTM SDR records, and the slower power-up sequence.
 */
rtm_fru_type_t rtm_fru_type(void);

/**
 * @brief Forget the cached identification (module plugged/unplugged).
 */
void rtm_fru_invalidate(void);

#endif
