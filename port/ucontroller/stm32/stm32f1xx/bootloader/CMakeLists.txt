# STM32_DEVICE / STM32_STARTUP are selected by the parent stm32f1xx scope
# from TARGET_CONTROLLER (MD_VL for STM32F100xx, MD otherwise).
if(NOT STM32_DEVICE)
  set(STM32_DEVICE "MD")
  set(STM32_STARTUP "startup_stm32f10x_md.s")
endif()

set(BOOTLOADER_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/stm32f10x_it.c
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/syscalls.c
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/sysmem.c
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/system_stm32f10x.c
  ${CMAKE_CURRENT_SOURCE_DIR}/Startup/${STM32_STARTUP}
)

set(CMAKE_ASM_FLAGS "")
set(CMAKE_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set(CMAKE_EXE_LINKER_FLAGS "")

set(CMAKE_C_FLAGS_DEBUG "-Og -g3 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g3")
set(CMAKE_ASM_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG})
set(CMAKE_ASM_FLAGS_RELEASE ${CMAKE_C_FLAGS_RELEASE})
set(CMAKE_ASM_FLAGS_RELWITHDEBINFO ${CMAKE_C_FLAGS_RELWITHDEBINFO})

set(STM32_BOOT_FLAGS "-mcpu=cortex-m3 -std=gnu11 -DDEGUG -DSTM32F10X_${STM32_DEVICE} -DUSE_STDPERIPH_DRIVER -ffunction-sections -fdata-sections -Wall -fstack-usage --specs=nano.specs -mfloat-abi=soft -mthumb")
set(STM32_BOOT_LINK_FLAGS "-mcpu=cortex-m3 -T\"${CMAKE_CURRENT_SOURCE_DIR}/../linker/${TARGET_CONTROLLER}_boot.ld\" --specs=nosys.specs -Wl,--gc-sections -static --specs=nano.specs -mfloat-abi=soft -mthumb -Wl,--start-group -lc -lm -Wl,--end-group")
# message(STATUS "STM32_BOOT_FLAGS: ${STM32_BOOT_FLAGS}")
# message(STATUS "STM32_BOOT_LINK_FLAGS: ${STM32_BOOT_LINK_FLAGS}")

add_executable(newboot ${BOOTLOADER_SRCS})
target_link_libraries(newboot STM32F1XX)
set_target_properties(newboot PROPERTIES
  COMPILE_FLAGS ${STM32_BOOT_FLAGS}
  SUFFIX ".elf"
  LINK_FLAGS ${STM32_BOOT_LINK_FLAGS}
)
target_include_directories(newboot PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
)
# message(STATUS "STM32_BOOT_INC: ${UCONTROLLER_HDRS}")
add_custom_command(TARGET newboot POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary newboot.elf newboot.bin
  COMMAND ${CMAKE_OBJCOPY} -O srec newboot.elf newboot.srec
  COMMAND ${CMAKE_SIZE} newboot.elf
  WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
  COMMENT "Generate a raw binary image of the bootloader"
)

##Generate hpm files if bin2hpm is installed
find_program(BIN2HPM NAMES "bin2hpm")
if(BIN2HPM)
  add_custom_command(TARGET newboot POST_BUILD
    COMMAND bin2hpm -c 0 -n -m 0x315A -p 0x00 newboot.bin -o newboot.hpm
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMENT "Creating HPM file from binary"
  )
endif()