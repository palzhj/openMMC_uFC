#ifndef RTM_FRU_H_
#define RTM_FRU_H_

/** Result of one RTM identification attempt (see rtm_fru_probe()). */
typedef enum {
    RTM_FRU_UNKNOWN = 0,   /**< no answer / blank FRU */
    RTM_FRU_URTM_V23,      /**< the uRTM v2.3 (the UCD90124A monitor board) */
    RTM_FRU_OTHER,         /**< a FRU answered but it is another board */
} rtm_fru_type_t;

/**
 * @brief Read the RTM FRU at 0x55 once and say what board it is.
 *
 * This is a single bus transaction set, with no caching and no timing: the
 * caller (the RTM manager task) owns the policy - it must only call this once
 * the RTM 12 V has been on long enough for the module's own MCU to start, and
 * it decides how many attempts are worth making.  Nothing here should ever be
 * called while the module may be unpowered: selecting the RTM bus with an
 * unpowered slave on it clamps SDA/SCL and wedges the whole I2C2 bus.
 *
 * The EEPROM is looked for twice over the RTM path: first through the uRTM's
 * own switch (0x70 ch5 -> 0x71 ch7, where the uRTM keeps its FRU), then
 * directly on the RTM bus (0x70 ch5 -> 0x55) for boards without that switch.
 * The module identifies as the uRTM only if its product area says "uRTM" and
 * version "2.3".
 *
 * @retval RTM_FRU_UNKNOWN nothing answered
 * @retval RTM_FRU_URTM_V23 the uRTM v2.3
 * @retval RTM_FRU_OTHER another board
 */
rtm_fru_type_t rtm_fru_probe(void);

#endif
