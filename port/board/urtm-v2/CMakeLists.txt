#List all modules used by this board
set(RTM_URTM_MODULES
  "PCA9554"
  "EEPROM_24XX512"
  )

set(RTM_URTM_PATH ${CMAKE_CURRENT_SOURCE_DIR})

#Include the modules sources
##Sensors SDR list
set( PROJ_SRCS ${PROJ_SRCS}
  ${RTM_URTM_PATH}/rtm_user.c
  ${RTM_URTM_PATH}/sdr_list.c
  ${RTM_URTM_PATH}/rtm_i2c_mapping.c
  )

#Include headers path
set(PROJ_HDRS ${PROJ_HDRS} ${RTM_URTM_PATH})

if(${TARGET_BOARD_NAME} MATCHES "u4fcp-v2")
  list(APPEND PROJ_HDRS ${RTM_URTM_PATH}/../u4fcp-v2/i2c_mapping.h)
endif()
#Set the variables in the main scope
set(TARGET_MODULES ${TARGET_MODULES} ${RTM_8SFP_MODULES} PARENT_SCOPE)

#Set the variables in the main scope
set(PROJ_SRCS ${PROJ_SRCS} PARENT_SCOPE)
set(PROJ_HDRS ${PROJ_HDRS} PARENT_SCOPE)
