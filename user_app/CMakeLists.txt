cmake_minimum_required(VERSION 3.13)

set(APP_VERSION_STRING "\"1.10.0\"")
set(CONFIG_FREERTOS 1)
set(CONFIG_FREERTOS_TIMER_RESOURCE_GPTMR 1)
set(CONFIG_EEPROM_EMULATION 1)
set(CONFIG_USE_FLASH_1M 1)
set(CONFIG_MBEDTLS 1)
set(CUSTOM_GCC_LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../linker/HPM5E31/gcc/user_app_flash_xip.ld)

if("${HPM_BUILD_TYPE}" STREQUAL "")
    set(HPM_BUILD_TYPE flash_xip)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../hpm_sdk_import.cmake)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

project(ethercat_charger_user_app)

sdk_compile_definitions(-DFLASH_XIP=1)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0)
sdk_compile_definitions(-DBOARD_SHOW_BANNER=0)
sdk_compile_definitions(-DCONFIG_USE_FLASH_1M=1)
sdk_compile_definitions(-DUSE_NONVECTOR_MODE=1)
sdk_compile_definitions(-DDISABLE_IRQ_PREEMPTIVE=0)
sdk_compile_definitions(-DUSE_SYSCALL_INTERRUPT_PRIORITY=1)
sdk_compile_definitions(-DECAT_SSC_TIMER_USE_RTOS_TICK=1)
sdk_compile_definitions(-D__ENABLE_FREERTOS=1)

sdk_inc(inc)
sdk_inc(../common)
sdk_inc(../common/charger)

sdk_app_src(src/main.c)
sdk_app_src(../common/charger/charger_app.c)

add_subdirectory($ENV{HPM_APP_BASE}/middleware/hpm_ota hpm_ota_build)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/ecat_sources.cmake)

generate_ide_projects()
