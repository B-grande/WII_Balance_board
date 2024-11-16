/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

// Include Bluetooth Classic headers
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"

// Include HID host headers
#include "esp_hidh.h"

#include "uuid_utils.h"
#include "esp_hid.h"


// Define Bluetooth address formatting macros
#define ESP_BD_ADDR_STR         "%02x:%02x:%02x:%02x:%02x:%02x"
#define ESP_BD_ADDR_HEX(addr)   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]


#define BALANCE_BOARD_NAME "Nintendo RVL-WBC-01"

static const char *TAG = "ESP_HIDH_DEMO";

static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

// Forward declaration of the GAP callback
void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

void hidh_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDH_OPEN_EVENT: {
        if (param->open.status == ESP_OK) {
            const uint8_t *bda = esp_hidh_dev_bda_get(param->open.dev);
            ESP_LOGI(TAG, ESP_BD_ADDR_STR " OPEN: %s", ESP_BD_ADDR_HEX(bda), esp_hidh_dev_name_get(param->open.dev));
            esp_hidh_dev_dump(param->open.dev, stdout);
        } else {
            ESP_LOGE(TAG, " OPEN failed!");
        }
        break;
    }
    case ESP_HIDH_BATTERY_EVENT: {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->battery.dev);
        ESP_LOGI(TAG, ESP_BD_ADDR_STR " BATTERY: %d%%", ESP_BD_ADDR_HEX(bda), param->battery.level);
        break;
    }
    case ESP_HIDH_INPUT_EVENT: {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->input.dev);
        ESP_LOGI(TAG, ESP_BD_ADDR_STR " INPUT: %8s, MAP: %2u, ID: %3u, Len: %d, Data:", ESP_BD_ADDR_HEX(bda), esp_hid_usage_str(param->input.usage), param->input.map_index, param->input.report_id, param->input.length);
        ESP_LOG_BUFFER_HEX(TAG, param->input.data, param->input.length);
        break;
    }
    case ESP_HIDH_FEATURE_EVENT: {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->feature.dev);
        ESP_LOGI(TAG, ESP_BD_ADDR_STR " FEATURE: %8s, MAP: %2u, ID: %3u, Len: %d", ESP_BD_ADDR_HEX(bda),
                 esp_hid_usage_str(param->feature.usage), param->feature.map_index, param->feature.report_id,
                 param->feature.length);
        ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
        break;
    }
    case ESP_HIDH_CLOSE_EVENT: {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->close.dev);
        ESP_LOGI(TAG, ESP_BD_ADDR_STR " CLOSE: %s", ESP_BD_ADDR_HEX(bda), esp_hidh_dev_name_get(param->close.dev));
        break;
    }
    default:
        ESP_LOGI(TAG, "EVENT: %d", event);
        break;
    }
}

#define SCAN_DURATION_SECONDS 5

void hid_demo_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting Bluetooth inquiry...");

    // Set the device to be discoverable and connectable
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE));

    // Start device discovery with inquiry length of 10 and unlimited responses
    ESP_ERROR_CHECK(esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0));

    // Wait for devices to be discovered (e.g., 15 seconds)
    vTaskDelay(pdMS_TO_TICKS(15000));

    // Stop device discovery
    ESP_ERROR_CHECK(esp_bt_gap_cancel_discovery());

    // Task can be deleted or you can implement additional logic
    vTaskDelete(NULL);
}
    
bool get_name_from_eir(uint8_t *eir, char *name, size_t len)
{
    uint8_t *ptr = eir;
    uint8_t eir_len = 0;

    while (ptr - eir < ESP_BT_GAP_EIR_DATA_LEN_MAX) {
        eir_len = *ptr++;
        if (eir_len == 0) {
            break;
        }
        uint8_t type = *ptr++;
        if (type == ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME || type == ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME) {
            size_t name_len = eir_len - 1;
            if (name_len > len - 1) {
                name_len = len - 1;
            }
            memcpy(name, ptr, name_len);
            name[name_len] = '\0';
            return true;
        }
        ptr += eir_len - 1;
    }
    return false;
}

void app_main(void)
{
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Release memory allocated for BLE, since we're using Classic BT
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    // Initialize the BT controller with the default configuration
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    // Enable the BT controller in Classic BT mode
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret) {
        ESP_LOGE(TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    // Initialize Bluedroid stack
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    // Register GAP callback for Bluetooth Classic
    ret = esp_bt_gap_register_callback(bt_app_gap_cb);
    if (ret) {
        ESP_LOGE(TAG, "%s gap register failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    // Initialize HID host
    esp_hidh_config_t config = {
        .callback = hidh_callback,
        .event_stack_size = 2048,
        .callback_arg = NULL,
    };
    ret = esp_hidh_init(&config);
    if(ret) {
        ESP_LOGE(TAG, "%s HID host init failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    // Start the HID demo task
    xTaskCreate(&hid_demo_task, "hid_task", 6 * 1024, NULL, 2, NULL);
}

void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char bda_str[18];

    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        // Device found
        bda2str(param->disc_res.bda, bda_str, sizeof(bda_str));
        ESP_LOGI(TAG, "Device found: %s", bda_str);

        // Check if the device name matches the Wii Balance Board
        for (int i = 0; i < param->disc_res.num_prop; i++) {
            esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];
            if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
                uint8_t *eir = (uint8_t *)prop->val;
                char name[248];
                if (get_name_from_eir(eir, name, sizeof(name))) {
                    ESP_LOGI(TAG, "Device name: %s", name);
                    if (strcmp(name, BALANCE_BOARD_NAME) == 0) {
                        ESP_LOGI(TAG, "Found Wii Balance Board: %s", name);

                        // Connect to the device
                        ESP_ERROR_CHECK(esp_hidh_dev_open(param->disc_res.bda, ESP_HID_TRANSPORT_BT, NULL));

                        // Optionally stop discovery
                        ESP_ERROR_CHECK(esp_bt_gap_cancel_discovery());
                    }
                }
            }
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT: {
        ESP_LOGI(TAG, "PIN code request received");
        esp_bt_pin_code_t pin_code;
        strcpy((char *)pin_code, "0000"); // Wii Balance Board default PIN code
        ESP_ERROR_CHECK(esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code));
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            ESP_LOGI(TAG, "Discovery stopped.");
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            ESP_LOGI(TAG, "Discovery started.");
        }
        break;
    }
    default:
        ESP_LOGI(TAG, "Unhandled GAP event: %d", event);
        break;
    }
}
