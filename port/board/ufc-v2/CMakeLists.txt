#Select which microcontroller and board are being used
if (NOT TARGET_CONTROLLER)
  set(TARGET_CONTROLLER "STM32F100CBT6" CACHE STRING "Target MMC Controller")
endif()

if (NOT TARGET_BOARD_NAME)
  set(TARGET_BOARD_NAME "uFC-v2" CACHE STRING "Board Name")
endif()

#List all modules used by this board
set(TARGET_MODULES
  "SDR"             # required by HOTSWAP_SENSOR (sdr_head / SDR entry)
  "PAYLOAD"
  "HOTSWAP_SENSOR"
  "EEPROM_24XX02"   # 24AA025E48 is a 2 Kbit (256 byte) EEPROM
  "FRU"
  "HPM"             # IPMI/PICMG HPM.1 self-update (MMC + bootloader only)
  "BOARD_CONFIG"
  )

# Online-writable FRU: keep the write handler, drop the runtime builder.
# Use -DFRU_READ_ONLY=ON for the smallest image (no FRU writes at all).
if (FRU_READ_ONLY)
  list(APPEND TARGET_MODULES "FRU_READ_ONLY")
elseif (FRU_BUILD)
  # One-time FRU builder + EEPROM write (-DFRU_BUILD=ON).
  set(MODULES_FLAGS "${MODULES_FLAGS} -DFRU_WRITE_EEPROM")
else()
  # Production: read the programmed FRU, keep the online IPMI write path.
  list(APPEND TARGET_MODULES "FRU_NO_BUILDER")
endif()

if (NOT DISABLE_WATCHDOG)
  list(APPEND TARGET_MODULES "WATCHDOG")
endif()

# Optional debug console on USART1 (PA9/PA10), 115200
if (UART_DEBUG)
  list(APPEND TARGET_MODULES "UART_DEBUG")
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

#Raw EEPROM access over IPMI (NetFn 0x32, cmds 0x12/0x13)
option(EEPROM_ACCESS "Enable raw EEPROM access over IPMI" OFF)
if (EEPROM_ACCESS)
  list(APPEND TARGET_MODULES "EEPROM_ACCESS")
endif()

option(CLOCK_PROGRAM "Program the Si5345 clock generator over IPMI" OFF)
if (CLOCK_PROGRAM)
  list(APPEND TARGET_MODULES "CLOCK_PROGRAM")
endif()

option(CLOCK_STORE "Clock configuration store in flash (FRU slots) over IPMI" OFF)
if (CLOCK_STORE)
  list(APPEND TARGET_MODULES "CLOCK_STORE")
endif()

option(I2C_ACCESS "Enable generic I2C transfers over IPMI" OFF)
if (I2C_ACCESS)
  list(APPEND TARGET_MODULES "I2C_ACCESS")
endif()

#Diagnostic PMBus probe for the UCD90320 (temporary, not for production)
option(UCD_PROBE "Build the UCD90320 PMBus diagnostic probe" OFF)
if (UCD_PROBE)
  set(MODULES_FLAGS "${MODULES_FLAGS} -DUCD_PROBE")
endif()

#Extra definitions
set(MODULES_FLAGS "${MODULES_FLAGS} -DFRU_MAX_SIZE=128")   # 24AA025E48, writable half

# 8 KB SRAM: fit all tasks + idle in the FreeRTOS heap (verified under Renode)
set(MODULES_FLAGS "${MODULES_FLAGS} -DIPMB_TX_TASK_STACK=96 -DIPMB_RX_TASK_STACK=96 -DIPMI_TASK_STACK=176 -DPAYLOAD_TASK_STACK=176 -DLED_TASK_STACK=96")

# Board identity for FreeRTOSConfig.h (heap size / idle stack)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUFC_V2" PARENT_SCOPE)

#Set the variables in the main scope
set(TARGET_MODULES ${TARGET_MODULES} PARENT_SCOPE)
set(MODULES_FLAGS ${MODULES_FLAGS} PARENT_SCOPE)
set(PROJ_HDRS ${PROJ_HDRS} ${BOARD_PATH})
set(PROJ_SRCS ${PROJ_SRCS} PARENT_SCOPE)
set(PROJ_HDRS ${PROJ_HDRS} PARENT_SCOPE)
