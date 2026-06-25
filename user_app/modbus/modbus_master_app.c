#include "modbus_master_app.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "agile_modbus.h"
#include "app_log.h"
#include "board.h"
#include "charger_app.h"
#include "hpm_clock_drv.h"
#include "hpm_dmamux_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_uart_drv.h"
#include "task.h"

#define MODBUS_UART                    BOARD_MODBUS_UART_BASE
#define MODBUS_UART_CLK_NAME           BOARD_MODBUS_UART_CLK_NAME
#define MODBUS_UART_BAUDRATE           BOARD_MODBUS_UART_BAUDRATE
#define MODBUS_UART_DMA_CONTROLLER     BOARD_APP_HDMA
#define MODBUS_UART_DMAMUX_CONTROLLER  BOARD_APP_DMAMUX
#define MODBUS_UART_RX_DMA_REQ         BOARD_MODBUS_UART_RX_DMA_REQ
#define MODBUS_UART_TX_DMA_REQ         BOARD_MODBUS_UART_TX_DMA_REQ
#define MODBUS_UART_RX_DMA_CH          (2U)
#define MODBUS_UART_TX_DMA_CH          (3U)
#define MODBUS_UART_RX_DMAMUX_CH       DMA_SOC_CHN_TO_DMAMUX_CHN(MODBUS_UART_DMA_CONTROLLER, MODBUS_UART_RX_DMA_CH)
#define MODBUS_UART_TX_DMAMUX_CH       DMA_SOC_CHN_TO_DMAMUX_CHN(MODBUS_UART_DMA_CONTROLLER, MODBUS_UART_TX_DMA_CH)

#define MODBUS_SLAVE_ID                (0x15U)
#define MODBUS_POLL_PERIOD_MS          (100U)
#define MODBUS_STEP_PERIOD_MS          (10U)
#define MODBUS_RESPONSE_TIMEOUT_MS     (300U)
#define MODBUS_FRAME_GAP_MS            (10U)
#define MODBUS_RX_BUFFER_SIZE          (256U)
#define MODBUS_CONTROL_BIT_COUNT       (7U)
#define MODBUS_STATUS_BIT_COUNT        (16U)
#define MODBUS_FEEDBACK_REG_COUNT      (6U)
#define MODBUS_RX_MIN_LEN              (5U)

#define MODBUS_STATUS_BIT_BASE         (0x0000U)
#define MODBUS_FEEDBACK_REG_BASE       (0x4002U)
#define MODBUS_CONTROL_BIT_BASE        (0x5000U)
#define MODBUS_CONTROL_MASK            ((1U << MODBUS_CONTROL_BIT_COUNT) - 1U)

typedef enum {
    modbus_state_send_control = 0,
    modbus_state_wait_control_tx_done,
    modbus_state_wait_control_response,
    modbus_state_process_control_response,
    modbus_state_send_status_read,
    modbus_state_wait_status_tx_done,
    modbus_state_wait_status_response,
    modbus_state_process_status_response,
    modbus_state_send_parameters_read,
    modbus_state_wait_parameters_tx_done,
    modbus_state_wait_parameters_response,
    modbus_state_process_parameters_response,
    modbus_state_poll_delay,
} modbus_state_t;

typedef struct {
    uint32_t front_index;
    uint32_t rear_index;
} modbus_rx_cursor_t;

static ATTR_PLACE_AT_NONCACHEABLE uint8_t s_modbus_send_buf[AGILE_MODBUS_MAX_ADU_LENGTH];
static ATTR_PLACE_AT_NONCACHEABLE uint8_t s_modbus_read_buf[AGILE_MODBUS_MAX_ADU_LENGTH];
static ATTR_PLACE_AT_NONCACHEABLE agile_modbus_rtu_t s_modbus_rtu;
static ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(4) uint8_t s_uart_rx_buf[MODBUS_RX_BUFFER_SIZE];
static ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(8) dma_linked_descriptor_t s_rx_descriptors[2];
static volatile ATTR_PLACE_AT_NONCACHEABLE modbus_rx_cursor_t s_rx_cursor;
static volatile bool s_tx_busy;
static uint8_t s_status_bits[MODBUS_STATUS_BIT_COUNT];
static uint16_t s_feedback_registers[MODBUS_FEEDBACK_REG_COUNT];
static uint16_t s_applied_control_word;
static uint16_t s_pending_control_mask;
static uint8_t s_active_control_bit;
static bool s_active_control_value;

static void modbus_uart_dma_init(void);

static void modbus_uart_init(void)
{
    uart_config_t config = { 0 };

    board_init_uart(MODBUS_UART);
    uart_default_config(MODBUS_UART, &config);
    config.fifo_enable = true;
    config.dma_enable = true;
    config.src_freq_in_hz = clock_get_frequency(MODBUS_UART_CLK_NAME);
    config.baudrate = MODBUS_UART_BAUDRATE;
    config.tx_fifo_level = uart_tx_fifo_trg_not_full;
    config.rx_fifo_level = uart_rx_fifo_trg_not_empty;

    if (uart_init(MODBUS_UART, &config) != status_success) {
        app_log_printf("modbus uart init failed\r\n");
        while (1) {
        }
    }

    memset((void *)&s_rx_cursor, 0, sizeof(s_rx_cursor));
    s_tx_busy = false;
    modbus_uart_dma_init();
}

static uint32_t modbus_uart_available(void)
{
    uint32_t dma_remaining_size;

    dma_remaining_size = dma_get_remaining_transfer_size(MODBUS_UART_DMA_CONTROLLER, MODBUS_UART_RX_DMA_CH);
    s_rx_cursor.rear_index = MODBUS_RX_BUFFER_SIZE - dma_remaining_size;

    if (s_rx_cursor.front_index > s_rx_cursor.rear_index) {
        return (MODBUS_RX_BUFFER_SIZE - s_rx_cursor.front_index) + s_rx_cursor.rear_index;
    }

    return s_rx_cursor.rear_index - s_rx_cursor.front_index;
}

static int modbus_uart_receive(uint8_t *data, uint32_t max_len)
{
    uint32_t rx_data_size;

    if ((data == NULL) || (max_len == 0U)) {
        return -1;
    }

    rx_data_size = modbus_uart_available();
    if (rx_data_size == 0U) {
        return 0;
    }

    if (rx_data_size > max_len) {
        rx_data_size = max_len;
    }

    for (uint32_t i = 0; i < rx_data_size; i++) {
        uint32_t index = s_rx_cursor.front_index + i;
        if (index >= MODBUS_RX_BUFFER_SIZE) {
            index -= MODBUS_RX_BUFFER_SIZE;
        }
        data[i] = s_uart_rx_buf[index];
    }

    s_rx_cursor.front_index += rx_data_size;
    if (s_rx_cursor.front_index >= MODBUS_RX_BUFFER_SIZE) {
        s_rx_cursor.front_index -= MODBUS_RX_BUFFER_SIZE;
    }

    return (int)rx_data_size;
}

static void modbus_uart_flush(void)
{
    (void)modbus_uart_available();
    s_rx_cursor.front_index = s_rx_cursor.rear_index;
}

static int modbus_uart_send(uint8_t *data, uint32_t len)
{
    if ((data == NULL) || (len == 0U) || s_tx_busy ||
        dma_channel_is_enable(MODBUS_UART_DMA_CONTROLLER, MODBUS_UART_TX_DMA_CH)) {
        return -1;
    }

    dma_set_transfer_size(MODBUS_UART_DMA_CONTROLLER, MODBUS_UART_TX_DMA_CH, len);
    dma_set_source_address(MODBUS_UART_DMA_CONTROLLER,
                           MODBUS_UART_TX_DMA_CH,
                           core_local_mem_to_sys_address(BOARD_RUNNING_CORE, (uint32_t)data));
    dma_enable_channel(MODBUS_UART_DMA_CONTROLLER, MODBUS_UART_TX_DMA_CH);
    s_tx_busy = true;

    return (int)len;
}

static bool modbus_uart_send_finish(void)
{
    if (!s_tx_busy) {
        return true;
    }

    if ((dma_get_remaining_transfer_size(MODBUS_UART_DMA_CONTROLLER, MODBUS_UART_TX_DMA_CH) == 0U) &&
        uart_check_status(MODBUS_UART, uart_stat_transmitter_empty)) {
        s_tx_busy = false;
        return true;
    }

    return false;
}

static void modbus_uart_dma_init(void)
{
    dma_channel_config_t rx_ch_config = { 0 };
    dma_handshake_config_t tx_ch_config = { 0 };

    dmamux_config(MODBUS_UART_DMAMUX_CONTROLLER, MODBUS_UART_RX_DMAMUX_CH, MODBUS_UART_RX_DMA_REQ, true);

    dma_default_channel_config(MODBUS_UART_DMA_CONTROLLER, &rx_ch_config);
    rx_ch_config.src_addr = (uint32_t)&MODBUS_UART->RBR;
    rx_ch_config.src_width = DMA_TRANSFER_WIDTH_BYTE;
    rx_ch_config.src_addr_ctrl = DMA_ADDRESS_CONTROL_FIXED;
    rx_ch_config.src_mode = DMA_HANDSHAKE_MODE_HANDSHAKE;
    rx_ch_config.dst_addr = core_local_mem_to_sys_address(BOARD_RUNNING_CORE, (uint32_t)s_uart_rx_buf);
    rx_ch_config.dst_width = DMA_TRANSFER_WIDTH_BYTE;
    rx_ch_config.dst_addr_ctrl = DMA_ADDRESS_CONTROL_INCREMENT;
    rx_ch_config.dst_mode = DMA_HANDSHAKE_MODE_NORMAL;
    rx_ch_config.size_in_byte = MODBUS_RX_BUFFER_SIZE;
    rx_ch_config.src_burst_size = DMA_NUM_TRANSFER_PER_BURST_1T;
    rx_ch_config.linked_ptr = core_local_mem_to_sys_address(BOARD_RUNNING_CORE, (uint32_t)&s_rx_descriptors[1]);
    if (dma_config_linked_descriptor(MODBUS_UART_DMA_CONTROLLER,
                                     &s_rx_descriptors[0],
                                     MODBUS_UART_RX_DMA_CH,
                                     &rx_ch_config) != status_success) {
        while (1) {
        }
    }

    rx_ch_config.linked_ptr = core_local_mem_to_sys_address(BOARD_RUNNING_CORE, (uint32_t)&s_rx_descriptors[0]);
    if (dma_config_linked_descriptor(MODBUS_UART_DMA_CONTROLLER,
                                     &s_rx_descriptors[1],
                                     MODBUS_UART_RX_DMA_CH,
                                     &rx_ch_config) != status_success) {
        while (1) {
        }
    }

    rx_ch_config.linked_ptr = core_local_mem_to_sys_address(BOARD_RUNNING_CORE, (uint32_t)&s_rx_descriptors[0]);
    if (dma_setup_channel(MODBUS_UART_DMA_CONTROLLER, MODBUS_UART_RX_DMA_CH, &rx_ch_config, true) != status_success) {
        while (1) {
        }
    }

    dmamux_config(MODBUS_UART_DMAMUX_CONTROLLER, MODBUS_UART_TX_DMAMUX_CH, MODBUS_UART_TX_DMA_REQ, true);
    dma_default_handshake_config(MODBUS_UART_DMA_CONTROLLER, &tx_ch_config);
    tx_ch_config.ch_index = MODBUS_UART_TX_DMA_CH;
    tx_ch_config.dst = (uint32_t)&MODBUS_UART->THR;
    tx_ch_config.dst_fixed = true;
    tx_ch_config.src_fixed = false;
    tx_ch_config.data_width = DMA_TRANSFER_WIDTH_BYTE;
    tx_ch_config.size_in_byte = AGILE_MODBUS_MAX_ADU_LENGTH;
    dma_setup_handshake(MODBUS_UART_DMA_CONTROLLER, &tx_ch_config, false);
}

static void modbus_refresh_pending_controls(void)
{
    charger_rxpdo_t rxpdo;

    charger_app_get_rxpdo(&rxpdo);
    s_pending_control_mask |= (rxpdo.control_word ^ s_applied_control_word) & MODBUS_CONTROL_MASK;
}

static bool modbus_select_pending_control(void)
{
    charger_rxpdo_t rxpdo;

    modbus_refresh_pending_controls();
    if (s_pending_control_mask == 0U) {
        return false;
    }

    charger_app_get_rxpdo(&rxpdo);
    for (uint8_t bit = 0U; bit < MODBUS_CONTROL_BIT_COUNT; bit++) {
        if ((s_pending_control_mask & (1U << bit)) != 0U) {
            s_active_control_bit = bit;
            s_active_control_value = (rxpdo.control_word & (1U << bit)) != 0U;
            return true;
        }
    }

    return false;
}

static void modbus_confirm_active_control(void)
{
    uint16_t mask = (uint16_t)(1U << s_active_control_bit);

    if (s_active_control_value) {
        s_applied_control_word |= mask;
    } else {
        s_applied_control_word &= (uint16_t)~mask;
    }
    s_pending_control_mask &= (uint16_t)~mask;
}

static uint16_t modbus_pack_status_word(const uint8_t *bits)
{
    uint16_t status_word = 0U;

    for (uint8_t bit = 0U; bit < MODBUS_STATUS_BIT_COUNT; bit++) {
        if (bits[bit] != 0U) {
            status_word |= (uint16_t)(1U << bit);
        }
    }

    return status_word;
}

static void modbus_apply_feedback(void)
{
    charger_txpdo_t txpdo;

    txpdo.status_word = modbus_pack_status_word(s_status_bits);
    txpdo.battery_level_x100 = s_feedback_registers[0];
    txpdo.sys_input_voltage_mv = s_feedback_registers[1];
    txpdo.battery_voltage_mv = s_feedback_registers[2];
    txpdo.charge_current_ma = s_feedback_registers[3];
    txpdo.discharge_current_ma = s_feedback_registers[4];
    txpdo.internal_resistance_mohm = s_feedback_registers[5];
    charger_app_set_modbus_feedback(&txpdo);
}

void modbus_master_init(void)
{
    agile_modbus_t *ctx = &s_modbus_rtu._ctx;

    memset(s_status_bits, 0, sizeof(s_status_bits));
    memset(s_feedback_registers, 0, sizeof(s_feedback_registers));
    s_applied_control_word = 0U;
    s_pending_control_mask = MODBUS_CONTROL_MASK;
    s_active_control_bit = 0U;
    s_active_control_value = false;

    modbus_uart_init();
    agile_modbus_rtu_init(&s_modbus_rtu,
                          s_modbus_send_buf,
                          sizeof(s_modbus_send_buf),
                          s_modbus_read_buf,
                          sizeof(s_modbus_read_buf));
    agile_modbus_set_slave(ctx, MODBUS_SLAVE_ID);
    app_log_printf("modbus rtu master uart2 pc08/pc09 %lu 8N1\r\n", (unsigned long)MODBUS_UART_BAUDRATE);
}

void modbus_master_task(void *pvParameters)
{
    agile_modbus_t *ctx = &s_modbus_rtu._ctx;
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t state_start = last_wake;
    modbus_state_t state = modbus_state_send_control;
    int read_len = 0;

    (void)pvParameters;

    modbus_master_init();

    while (1) {
        TickType_t now = xTaskGetTickCount();

        switch (state) {
        case modbus_state_send_control: {
            int send_len;

            if (!modbus_select_pending_control()) {
                state = modbus_state_send_status_read;
                state_start = now;
                break;
            }

            modbus_uart_flush();
            send_len = agile_modbus_serialize_write_bit(ctx,
                                                       MODBUS_CONTROL_BIT_BASE + s_active_control_bit,
                                                       s_active_control_value);
            if (modbus_uart_send(ctx->send_buf, (uint32_t)send_len) < 0) {
                app_log_printf("modbus control send failed\r\n");
                state = modbus_state_poll_delay;
            } else {
                state = modbus_state_wait_control_tx_done;
            }
            state_start = now;
            break;
        }
        case modbus_state_wait_control_tx_done:
            if (modbus_uart_send_finish()) {
                state = modbus_state_wait_control_response;
                state_start = now;
            } else if ((now - state_start) > pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS)) {
                app_log_printf("modbus control tx timeout\r\n");
                state = modbus_state_poll_delay;
                state_start = now;
            }
            break;
        case modbus_state_wait_control_response:
            read_len = modbus_uart_receive(ctx->read_buf, ctx->read_bufsz);
            if (read_len >= MODBUS_RX_MIN_LEN) {
                if (modbus_uart_available() == 0U) {
                    state = modbus_state_process_control_response;
                }
            } else if ((now - state_start) > pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS)) {
                app_log_printf("modbus control timeout\r\n");
                state = modbus_state_poll_delay;
                state_start = now;
            }
            break;
        case modbus_state_process_control_response: {
            int rc = agile_modbus_deserialize_write_bit(ctx, read_len);
            if (rc < 0) {
                app_log_printf("modbus control parse failed rc:%d len:%d\r\n", rc, read_len);
                state = modbus_state_poll_delay;
            } else {
                modbus_confirm_active_control();
                state = modbus_state_send_control;
            }
            state_start = now;
            break;
        }
        case modbus_state_send_status_read: {
            int send_len;
            modbus_uart_flush();
            send_len = agile_modbus_serialize_read_input_bits(ctx,
                                                              MODBUS_STATUS_BIT_BASE,
                                                              MODBUS_STATUS_BIT_COUNT);
            if (modbus_uart_send(ctx->send_buf, (uint32_t)send_len) < 0) {
                app_log_printf("modbus status read send failed\r\n");
                state = modbus_state_poll_delay;
            } else {
                state = modbus_state_wait_status_tx_done;
            }
            state_start = now;
            break;
        }
        case modbus_state_wait_status_tx_done:
            if (modbus_uart_send_finish()) {
                state = modbus_state_wait_status_response;
                state_start = now;
            } else if ((now - state_start) > pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS)) {
                app_log_printf("modbus status tx timeout\r\n");
                state = modbus_state_poll_delay;
                state_start = now;
            }
            break;
        case modbus_state_wait_status_response:
            read_len = modbus_uart_receive(ctx->read_buf, ctx->read_bufsz);
            if (read_len >= MODBUS_RX_MIN_LEN) {
                if (modbus_uart_available() == 0U) {
                    state = modbus_state_process_status_response;
                }
            } else if ((now - state_start) > pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS)) {
                app_log_printf("modbus status timeout\r\n");
                state = modbus_state_poll_delay;
                state_start = now;
            }
            break;
        case modbus_state_process_status_response: {
            int rc = agile_modbus_deserialize_read_input_bits(ctx, read_len, s_status_bits);
            if (rc < 0) {
                app_log_printf("modbus status parse failed rc:%d len:%d\r\n", rc, read_len);
                state = modbus_state_poll_delay;
            } else {
                state = modbus_state_send_parameters_read;
            }
            state_start = now;
            break;
        }
        case modbus_state_send_parameters_read: {
            int send_len;
            modbus_uart_flush();
            send_len = agile_modbus_serialize_read_registers(ctx,
                                                              MODBUS_FEEDBACK_REG_BASE,
                                                              MODBUS_FEEDBACK_REG_COUNT);
            if (modbus_uart_send(ctx->send_buf, (uint32_t)send_len) < 0) {
                app_log_printf("modbus parameters read send failed\r\n");
                state = modbus_state_poll_delay;
            } else {
                state = modbus_state_wait_parameters_tx_done;
            }
            state_start = now;
            break;
        }
        case modbus_state_wait_parameters_tx_done:
            if (modbus_uart_send_finish()) {
                state = modbus_state_wait_parameters_response;
                state_start = now;
            } else if ((now - state_start) > pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS)) {
                app_log_printf("modbus parameters tx timeout\r\n");
                state = modbus_state_poll_delay;
                state_start = now;
            }
            break;
        case modbus_state_wait_parameters_response:
            read_len = modbus_uart_receive(ctx->read_buf, ctx->read_bufsz);
            if (read_len >= MODBUS_RX_MIN_LEN) {
                if (modbus_uart_available() == 0U) {
                    state = modbus_state_process_parameters_response;
                }
            } else if ((now - state_start) > pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS)) {
                app_log_printf("modbus parameters timeout\r\n");
                state = modbus_state_poll_delay;
                state_start = now;
            }
            break;
        case modbus_state_process_parameters_response: {
            int rc = agile_modbus_deserialize_read_registers(ctx, read_len, s_feedback_registers);
            if (rc < 0) {
                app_log_printf("modbus parameters parse failed rc:%d len:%d\r\n", rc, read_len);
            } else {
                modbus_apply_feedback();
            }
            state = modbus_state_poll_delay;
            state_start = now;
            break;
        }
        case modbus_state_poll_delay:
        default:
            if ((now - state_start) >= pdMS_TO_TICKS(MODBUS_POLL_PERIOD_MS)) {
                state = modbus_state_send_control;
                state_start = now;
            }
            break;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MODBUS_STEP_PERIOD_MS));
    }
}
