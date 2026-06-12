cmake_minimum_required(VERSION 3.13)

set(APP_VERSION_STRING "\"1.10.0\"")
set(APP_NAME ethercat_charger_bootuser)
set(INCLUDE_BOOTHEADER 1)
set(CONFIG_USE_FLASH_1M 1)
set(CONFIG_MBEDTLS 1)

if("${HPM_BUILD_TYPE}" STREQUAL "")
    set(HPM_BUILD_TYPE flash_xip)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../hpm_sdk_import.cmake)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

project(ethercat_charger_bootuser)

sdk_compile_definitions(-DFLASH_XIP=1)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0)
sdk_compile_definitions(-DBOOTUSER_ENABLE=1)
sdk_compile_definitions(-DCONFIG_USE_FLASH_1M=1)
sdk_compile_definitions(-DBOOTUSER_PBUTN_BOOTMODE=0)

sdk_inc(../common)
sdk_app_src(src/boot_user.c)

add_subdirectory($ENV{HPM_APP_BASE}/middleware/hpm_ota hpm_ota_build)
