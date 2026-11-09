#Select which microcontroller and board are being used
if (NOT TARGET_CONTROLLER)
  set(TARGET_CONTROLLER "STM32F103CBT6" CACHE STRING "Target MMC Controller")
endif()

if (NOT TARGET_BOARD_NAME)
  set(TARGET_BOARD_NAME "u4FCP-v2" CACHE STRING "Board Name")
endif()

#List all modules used by this board
set(TARGET_MODULES
  "BOARD_CONFIG"
  "UART_DEBUG"
  "FRU"
  "EEPROM_24XX512"
  "SDR"
  "HOTSWAP_SENSOR"
  "PAYLOAD"
  "VIO_TPL0102"
  "ADN4604"
  # "CLOCK_CONFIG"
  "UCD90XXX"
  # "MCP23016"
  # "ADC"
  "HPM"
  # "SYSUTILS"
  # "RTM"
  )

option(EEPROM_ACCESS "Enable raw EEPROM access over IPMI" ON)
if (EEPROM_ACCESS)
  list(APPEND TARGET_MODULES "EEPROM_ACCESS")
  # the u4FCP EEPROM is 64 KB (CAT24C512) / 128 KB (AT24CM01); the raw
  # access commands use 16-bit offsets, so allow the whole 64 KB window
  set(MODULES_FLAGS "${MODULES_FLAGS} -DEEPROM_ACCESS_SIZE=65536u")
endif()
option(CLOCK_PROGRAM "Program the Si5345 clock generator over IPMI" ON)
if (CLOCK_PROGRAM)
  list(APPEND TARGET_MODULES "CLOCK_PROGRAM")
endif()
option(CLOCK_STORE "Clock configuration store in flash (FRU slots) over IPMI" ON)
if (CLOCK_STORE)
  list(APPEND TARGET_MODULES "CLOCK_STORE")
  # the u4FCP keeps the clock configuration in its 64 KB CAT24C512
  set(MODULES_FLAGS "${MODULES_FLAGS} -DCLOCK_STORE_EEPROM")
endif()
option(I2C_ACCESS "Enable generic I2C transfers over IPMI" ON)
if (I2C_ACCESS)
  list(APPEND TARGET_MODULES "I2C_ACCESS")
endif()

if (NOT DISABLE_WATCHDOG)
  list(APPEND TARGET_MODULES "WATCHDOG")
endif()

set(BOARD_PATH ${CMAKE_CURRENT_SOURCE_DIR})

#Include the modules sources
#Sensors SDR list
set( PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/sdr_list.c )
set( PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/i2c_mapping.c )
set( PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/pin_mapping.c )

if (";${TARGET_MODULES};" MATCHES ";PAYLOAD;")
  set(PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/payload.c)
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_PAYLOAD")
endif()

if (";${TARGET_MODULES};" MATCHES ";RTM;")
  set(PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/rtm.c)
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_RTM")
endif()

if (";${TARGET_MODULES};" MATCHES ";ADC;")
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_ADC")
endif()

if (";${TARGET_MODULES};" MATCHES ";BOARD_CONFIG;")
  set(PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/board_config.c)
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_BOARD_CONFIG")
endif()

#Extra definitions
# Serve the FRU built from user_amc_fru.h (the board's factory image).
option(FRU_USE_COMPILED "Build the FRU from user_amc_fru.h instead of reading the EEPROM" ON)
if (FRU_USE_COMPILED)
  set(MODULES_FLAGS "${MODULES_FLAGS} -DFRU_USE_COMPILED")
endif()

# The application is close to the 50 KB region: optimise for size.
set(CMAKE_C_FLAGS_RELEASE "-Os" PARENT_SCOPE)
# 20 KB SRAM: 12 KB FreeRTOS heap leaves a ~6 KB main/exception stack
# (RENODE_SIM is a generic top-level option)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DconfigTOTAL_HEAP_SIZE=12288 -DUSE_RAM_DIAG -DFRU_MAX_SIZE=512" PARENT_SCOPE)
if (FRU_WRITE_EEPROM)
  message(STATUS "FRU EEPROM will be written if no valid data is found!")
  set(MODULES_FLAGS "${MODULES_FLAGS} -DFRU_WRITE_EEPROM")
  set(FRU_WRITE_EEPROM false CACHE BOOL "Set this flag to enable writing the board ID on the EEPROM" FORCE)
endif()

#Set the variables in the main scope
set(TARGET_MODULES ${TARGET_MODULES} PARENT_SCOPE)
set(MODULES_FLAGS ${MODULES_FLAGS} PARENT_SCOPE)
set(PROJ_HDRS ${PROJ_HDRS} ${BOARD_PATH})
set(PROJ_SRCS ${PROJ_SRCS} PARENT_SCOPE)
set(PROJ_HDRS ${PROJ_HDRS} PARENT_SCOPE)
