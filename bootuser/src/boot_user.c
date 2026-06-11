#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "hpm_appheader.h"
#include "hpm_flash.h"
#include "hpm_flashmap.h"
#include "hpm_gpio_drv.h"
#include "hpm_ota.h"
#include "ota_port.h"

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

static bool bootuser_check_pbutn_bootmode(void)
{
    uint32_t pressed_count = 0;

    gpio_set_pin_input(BOARD_APP_GPIO_CTRL, BOARD_APP_GPIO_INDEX, BOARD_APP_GPIO_PIN);
    for (uint8_t i = 0; i < 10U; i++) {
        if (gpio_read_pin(BOARD_APP_GPIO_CTRL, BOARD_APP_GPIO_INDEX, BOARD_APP_GPIO_PIN) == 0U) {
            pressed_count++;
        }
        board_delay_ms(10U);
    }

    return pressed_count > 9U;
}

static bool bootuser_read_app_header(uint8_t app_index, hpm_app_header_t *header)
{
    if (hpm_ota_get_header_info_of_app(app_index, header) < 0) {
        return false;
    }

    return header->magic == HPM_APP_FILE_FLAG_MAGIC;
}

static bool bootuser_verify_app_valid(uint8_t app_index, const hpm_app_header_t *header)
{
    uint32_t app_addr = (app_index == HPM_APP1) ? FLASH_USER_APP1_ADDR : FLASH_USER_APP2_ADDR;

    if (header == NULL || header->magic != HPM_APP_FILE_FLAG_MAGIC) {
        return false;
    }

    if (header->hash_enable != 0U && header->pwr_hash != 0U) {
        if (hpm_ota_package_verify(app_addr + sizeof(hpm_app_header_t) + FLASH_ADDR_BASE,
                                   header->len,
                                   (hpm_app_header_t *)header)) {
            printf("APP%d verify SUCCESS!\r\n", app_index);
            return true;
        }

        printf("APP%d verify FAIL!\r\n", app_index);
        return false;
    }

    return true;
}

static int bootuser_select_jump_app(bool pbutn_enable)
{
    uint8_t valid_mask = 0U;
    hpm_app_header_t app1_header = {0};
    hpm_app_header_t app2_header = {0};

    if (pbutn_enable && bootuser_check_pbutn_bootmode()) {
        printf("PBUT pressed, stay in bootuser mode.\r\n");
        return -1;
    }

    if (bootuser_read_app_header(HPM_APP1, &app1_header)) {
        valid_mask |= BIT(0);
    }
    if (bootuser_read_app_header(HPM_APP2, &app2_header)) {
        valid_mask |= BIT(1);
    }

    if (valid_mask == (BIT(0) | BIT(1))) {
        printf("APP1 ver:%u, APP2 ver:%u\r\n", app1_header.version, app2_header.version);
        if (app1_header.version >= app2_header.version) {
            if (bootuser_verify_app_valid(HPM_APP1, &app1_header)) {
                return HPM_APP1;
            }
            if (bootuser_verify_app_valid(HPM_APP2, &app2_header)) {
                return HPM_APP2;
            }
        } else {
            if (bootuser_verify_app_valid(HPM_APP2, &app2_header)) {
                return HPM_APP2;
            }
            if (bootuser_verify_app_valid(HPM_APP1, &app1_header)) {
                return HPM_APP1;
            }
        }
    } else if ((valid_mask & BIT(0)) != 0U) {
        if (bootuser_verify_app_valid(HPM_APP1, &app1_header)) {
            return HPM_APP1;
        }
    } else if ((valid_mask & BIT(1)) != 0U) {
        if (bootuser_verify_app_valid(HPM_APP2, &app2_header)) {
            return HPM_APP2;
        }
    }

    return -1;
}

int main(void)
{
    int boot_index;

    board_init();
    board_init_gpio_pins();

    printf("EtherCAT charger bootuser\r\n");

    if (hpm_flash_init() == 0) {
        printf("Flash init failed, stay in bootuser mode.\r\n");
    } else {
        boot_index = bootuser_select_jump_app(true);
        printf("APP index:%d\r\n", boot_index);
        if (boot_index >= 0) {
            hpm_appindex_jump((uint8_t)boot_index);
        }
    }

    printf("Bootuser FoE recovery mode.\r\n");
    if (hpm_ota_init() != 0) {
        printf("FoE OTA init failed.\r\n");
        while (1) {
        }
    }

    while (1) {
        hpm_ota_polling_handle();
    }
}

