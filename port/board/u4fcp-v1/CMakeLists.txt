# u4FCP-v1: same hardware as u4FCP-v2 but the STM32F103C8T6 (64 KB flash).
# The board sources are shared with the v2 port (identical devices), only the
# controller and the flash layout differ.
if (NOT TARGET_CONTROLLER)
  set(TARGET_CONTROLLER "STM32F103C8T6" CACHE STRING "Target MMC Controller")
endif()

if (NOT TARGET_BOARD_NAME)
  set(TARGET_BOARD_NAME "u4FCP-v1" CACHE STRING "Board Name")
endif()

# independent copy of the u4FCP board sources (identical devices, but this
# port can diverge from the v2)
set(BOARD_PATH ${CMAKE_CURRENT_SOURCE_DIR})

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
  )

if (NOT DISABLE_WATCHDOG)
  list(APPEND TARGET_MODULES "WATCHDOG")
endif()

set( PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/sdr_list.c )
set( PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/i2c_mapping.c )
set( PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/pin_mapping.c )

if (";${TARGET_MODULES};" MATCHES ";PAYLOAD;")
  set(PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/payload.c)
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_PAYLOAD")
endif()

if (";${TARGET_MODULES};" MATCHES ";BOARD_CONFIG;")
  set(PROJ_SRCS ${PROJ_SRCS} ${BOARD_PATH}/board_config.c)
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_BOARD_CONFIG")
endif()

if (";${TARGET_MODULES};" MATCHES ";ADN4604;")
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_ADN4604")
endif()

if (";${TARGET_MODULES};" MATCHES ";VIO_TPL0102;")
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_VIO_TPL0102")
endif()

if (";${TARGET_MODULES};" MATCHES ";EEPROM_24XX512;")
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_EEPROM_24XX512")
endif()

if (";${TARGET_MODULES};" MATCHES ";HOTSWAP_SENSOR;")
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_HOTSWAP")
endif()

if (";${TARGET_MODULES};" MATCHES ";UCD90XXX;")
  set(MODULES_FLAGS "${MODULES_FLAGS} -DMODULE_UCD90XXX")
endif()

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
option(CLOCK_STORE "Clock configuration store (EEPROM slots) over IPMI" ON)
if (CLOCK_STORE)
  list(APPEND TARGET_MODULES "CLOCK_STORE")
  set(MODULES_FLAGS "${MODULES_FLAGS} -DCLOCK_STORE_EEPROM")
endif()
option(I2C_ACCESS "Enable generic I2C transfers over IPMI" ON)
if (I2C_ACCESS)
  list(APPEND TARGET_MODULES "I2C_ACCESS")
endif()

#Extra definitions
# v1 populates an AT24CM01 (1 Mbit = 128 KB, 256-byte pages, 17 address bits:
# A16 is carried in the slave address, so the upper 64 KB uses 0x55).
set(MODULES_FLAGS "${MODULES_FLAGS} -DEEPROM_24CM01")
# 64 KB flash: the application region is 55 KB (no staging), so compile for
# size and keep the FRU in the EEPROM (+ compiled FRU fallback).
option(FRU_USE_COMPILED "Build the FRU from user_amc_fru.h instead of reading the EEPROM" OFF)
if (FRU_USE_COMPILED)
  set(MODULES_FLAGS "${MODULES_FLAGS} -DFRU_USE_COMPILED")
endif()
set(CMAKE_C_FLAGS_RELEASE "-Os" PARENT_SCOPE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DconfigTOTAL_HEAP_SIZE=12288 -DUSE_RAM_DIAG -DFRU_MAX_SIZE=512" PARENT_SCOPE)

if (FRU_WRITE_EEPROM)
  message(STATUS "FRU EEPROM will be written if no valid data is found!")
  set(MODULES_FLAGS "${MODULES_FLAGS} -DFRU_WRITE_EEPROM")
  set(FRU_WRITE_EEPROM false CACHE BOOL "Set this flag to enable writing the board ID on the EEPROM" FORCE)
endif()

set(TARGET_MODULES ${TARGET_MODULES} PARENT_SCOPE)
set(MODULES_FLAGS ${MODULES_FLAGS} PARENT_SCOPE)
set(PROJ_SRCS ${PROJ_SRCS} PARENT_SCOPE)
set(PROJ_HDRS ${PROJ_HDRS} PARENT_SCOPE)
set(PROJ_HDRS ${PROJ_HDRS} ${BOARD_PATH} ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)
