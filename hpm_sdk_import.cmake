# Shared import helpers for the standalone EtherCAT charger projects.

if(NOT DEFINED ENV{HPM_SDK_BASE})
    message(FATAL_ERROR "Please set HPM_SDK_BASE to the HPM SDK path")
endif()

if(NOT DEFINED ENV{HPM_APP_BASE})
    message(FATAL_ERROR "Please set HPM_APP_BASE to the hpm_apps path")
endif()

set(HPM_SDK_BASE $ENV{HPM_SDK_BASE})
set(HPM_APP_BASE $ENV{HPM_APP_BASE})

if(NOT DEFINED BOARD_SEARCH_PATH)
    set(BOARD_SEARCH_PATH ${CMAKE_CURRENT_LIST_DIR}/boards)
endif()

