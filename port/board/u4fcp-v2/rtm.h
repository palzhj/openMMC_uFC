#ifndef RTM_BOARD_H_
#define RTM_BOARD_H_

#include <stdbool.h>
#include "mmc_error.h"

void rtm_init(void);
bool rtm_present(void);
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
