#ifndef RTM_BOARD_H_
#define RTM_BOARD_H_

#include <stdbool.h>
#include "mmc_error.h"

void rtm_init(void);
bool rtm_present(void);
/**
 * @brief Did an RTM just get plugged in (RTM_PS# high -> low)?
 *
 * Consumed once per transition; the identification cache is dropped so the
 * new module is identified again.
 */
bool rtm_plugged_edge(void);
/**
 * @brief Is the module in Zone 3 the uRTM v2.3 (identified from its FRU)?
 *
 * Only this board has the FMC2/FMC3 monitor UCD, so only it gets the RTM SDR
 * records.
 */
bool rtm_is_urtm_v23(void);

/**
 * @brief May RTM_INT_B be read as "handle opened"?
 *
 * Only for a module that identified itself as the uRTM v2.3.  RTM_INT# and
 * the RTM 12 V / MP fault pins are uRTM signals; other RTMs drive RTM_INT#
 * from their own logic (the LattePanda carrier from its TCA9554A) and leave
 * the fault pins undriven, and an unidentified module must not be switched
 * off by either - RTM_PS# alone decides whether it is powered.
 */
bool rtm_handle_trusted(void);
/**
 * @brief Is the uRTM signalling attention (RTM_INT_B, PA3, active low)?
 *
 * The uRTM drives this line low when its handle is opened, but also while it
 * is not running: it is powered from the 12 V rail the AMC switches, so a
 * powered-off RTM looks the same.  RTM_PS# (PA8) only reports that the module
 * is plugged.
 */
bool rtm_attention(void);

/**
 * @brief Is the uRTM plugged and running (12 V on, no attention)?
 */
bool rtm_alive(void);
bool rtm_12v_is_on(void);
bool rtm_config_window(void);
bool rtm_handle_removed(void);
mmc_err rtm_overcurrent_check(void);
mmc_err rtm_enable_12v(void);
mmc_err rtm_disable_12v(void);

#endif
