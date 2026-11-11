#ifndef RTM_BOARD_H_
#define RTM_BOARD_H_

#include <stdbool.h>
#include "mmc_error.h"

void rtm_init(void);
bool rtm_present(void);
bool rtm_12v_is_on(void);
bool rtm_config_window(void);
bool rtm_handle_removed(void);
mmc_err rtm_overcurrent_check(void);
mmc_err rtm_enable_12v(void);
mmc_err rtm_disable_12v(void);

#endif
