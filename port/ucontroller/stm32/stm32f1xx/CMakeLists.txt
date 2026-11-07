set(STM32F1XX_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(STM32F1XX_SPL_SRCPATH ${STM32F1XX_PATH}/Libraries/STM32F10x_StdPeriph_Driver/src)
set(STM32F1XX_SPL_INCPATH ${STM32F1XX_PATH}/Libraries/STM32F10x_StdPeriph_Driver/inc)
set(CMSIS_INCPATH ${STM32F1XX_PATH}/Libraries/CMSIS/CM3/CoreSupport)

#Silence GCC Warnings
# add_compile_options( -Wno-switch -Wno-unused-but-set-variable )

# Select the STM32F1 device family for the target MCU. This picks the matching
# CMSIS startup file and the STM32F10X_* define used by the StdPeriph driver.
#   - STM32F100xx are medium-density *value line* (MD_VL) parts:
#     128 KB flash, 8 KB SRAM, 24 MHz max, 1 KB flash pages.
#   - All other STM32F1 parts used by this port are medium density (MD).
if(TARGET_CONTROLLER MATCHES "^STM32F100")
  set(STM32_DEVICE "MD_VL")
  set(STM32_STARTUP "startup_stm32f10x_md_vl.s")
else()
  set(STM32_DEVICE "MD")
  set(STM32_STARTUP "startup_stm32f10x_md.s")
endif()
message(STATUS "STM32 device family: STM32F10X_${STM32_DEVICE} (${STM32_STARTUP})")

set(UCONTROLLER_SRCS
  ${STM32F1XX_PATH}/${STM32_STARTUP}
  ${STM32F1XX_PATH}/system_stm32f10x.c
  ${STM32F1XX_PATH}/stm32f10x_it.c
  ${STM32F1XX_PATH}/sysmem.c
  ${STM32F1XX_PATH}/syscalls.c
  ${STM32F1XX_PATH}/stm32_pincfg.c
  )

set(UCONTROLLER_HDRS
  ${STM32F1XX_PATH}
  ${STM32F1XX_SPL_INCPATH}
  ${CMSIS_INCPATH}
  )

set(LIBSTM32F1XX_SRCS
  ${STM32F1XX_SPL_SRCPATH}/misc.c
  ${STM32F1XX_SPL_SRCPATH}/stm32f10x_dma.c
  ${STM32F1XX_SPL_SRCPATH}/stm32f10x_exti.c
  ${STM32F1XX_SPL_SRCPATH}/stm32f10x_flash.c
  ${STM32F1XX_SPL_SRCPATH}/stm32f10x_gpio.c
  ${STM32F1XX_SPL_SRCPATH}/stm32f10x_i2c.c
  ${STM32F1XX_SPL_SRCPATH}/stm32f10x_pwr.c
  ${STM32F1XX_SPL_SRCPATH}/stm32f10x_rcc.c
  ${STM32F1XX_SPL_SRCPATH}/stm32f10x_tim.c
  ${STM32F1XX_SPL_SRCPATH}/stm32f10x_usart.c
  ${STM32F1XX_SPL_SRCPATH}/stm32f10x_wwdg.c
  ${STM32F1XX_PATH}/Libraries/CMSIS/CM3/CoreSupport/core_cm3.c

  ${STM32F1XX_PATH}/stm32_i2c.c
  ${STM32F1XX_PATH}/stm32_hpm.c
  # ${STM32F1XX_PATH}/lpc17_hpm.c
  )

if( UART_RINGBUFFER )
  set(LIBSTM32F1XX_SRCS
    ${LIBSTM32F1XX_SRCS}
    # ${STM32F1XX_SPL_SRCPATH}/ring_buffer.c
    # ${STM32F1XX_PATH}/lpc17_uartrb.c
  )
  set(MODULES_FLAGS ${MODULES_FLAGS} -DUART_RINGBUFFER PARENT_SCOPE)
else()
  set(LIBSTM32F1XX_SRCS
    ${LIBSTM32F1XX_SRCS}
    ${STM32F1XX_PATH}/stm32_uart.c
  )
endif()

set(TARGET_CPU      "cortex-m3")

set(STM32_FLAGS ${STM32_FLAGS} -mcpu=${TARGET_CPU})
set(STM32_FLAGS ${STM32_FLAGS} -DSTM32F10X_${STM32_DEVICE})
set(STM32_FLAGS ${STM32_FLAGS} -DUSE_STDPERIPH_DRIVER)
set(STM32_FLAGS ${STM32_FLAGS} -mthumb -mthumb-interwork -mfloat-abi=soft)

string(REGEX REPLACE ";" " " STM32_FLAGS "${STM32_FLAGS}")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${STM32_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CMAKE_C_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS}" PARENT_SCOPE)

set(UCONTROLLER_SRCS ${UCONTROLLER_SRCS} PARENT_SCOPE)
set(UCONTROLLER_HDRS ${UCONTROLLER_HDRS} PARENT_SCOPE)
set(UCONTROLLER_APP_LD_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linker/${TARGET_CONTROLLER}_app.ld" PARENT_SCOPE)

add_library(STM32F1XX STATIC ${LIBSTM32F1XX_SRCS})

target_link_libraries(STM32F1XX PUBLIC FreeRTOS)
target_include_directories(STM32F1XX PUBLIC ${UCONTROLLER_HDRS})
target_include_directories(STM32F1XX PUBLIC ${PROJ_HDRS})
# stm32_hpm.c uses the HPM/IPMI interfaces from modules/
target_include_directories(STM32F1XX PUBLIC ${CMAKE_SOURCE_DIR}/modules)

add_subdirectory(bootloader)
