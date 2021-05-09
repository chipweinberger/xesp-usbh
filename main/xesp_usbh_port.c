
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_intr_alloc.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_rom_gpio.h"
#include "esp_log.h"

#include "soc/gpio_pins.h"
#include "soc/gpio_sig_map.h"
#include "hal/usbh_ll.h"
#include "hcd.h"

#include "usb_utils.h"
#include "xesp_usbh.h"

#include "xesp_usbh_port.h"

#define PORT_EVENT_QUEUE_LEN         10

struct event_msg_t{
    hcd_port_handle_t port;
    hcd_port_event_t event;
};

typedef struct event_msg_t event_msg_t;

static const char* TAG = "xesp usb port";

xesp_usbh_port_callback_t* port_event_callback;

static QueueHandle_t port_evt_queue;

static void port_phy_force_conn_state(bool connected, TickType_t delay_ticks)
{
    ESP_LOGI(TAG, "port_phy_force_conn_state");

    vTaskDelay(delay_ticks);
    usb_wrap_dev_t *wrap = &USB_WRAP;
    if (connected) {
        //Swap back to internal PHY that is connected to a devicee
        wrap->otg_conf.phy_sel = 0;
    } else {
        //Set externa PHY input signals to fixed voltage levels mimicing a disconnected state
        esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, USB_EXTPHY_VP_IDX, false);
        esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, USB_EXTPHY_VM_IDX, false);
        esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, USB_EXTPHY_RCV_IDX, false);
        //Swap to the external PHY
        wrap->otg_conf.phy_sel = 1;
    }
}

static bool port_isr_callback(hcd_port_handle_t port, hcd_port_event_t event, void *user_arg, bool in_isr)
{
    QueueHandle_t port_evt_queue = (QueueHandle_t)user_arg;
    event_msg_t msg = {
        .port = port,
        .event = event,
    };

    BaseType_t xTaskWoken = pdFALSE;
    xQueueSendFromISR(port_evt_queue, &msg, &xTaskWoken);
    return (xTaskWoken == pdTRUE);
}

static void port_event_task(void* p)
{
    ESP_LOGI(TAG, "port event task started");

    event_msg_t msg;
    while(1){
        xQueueReceive(port_evt_queue, &msg, portMAX_DELAY);

        ESP_LOGI(TAG, "port: %p event: %s", msg.port, hcd_port_event_str(msg.event));

        // call callback
        port_event_callback(msg.port, msg.event);   
    }
}


hcd_port_handle_t xesp_usbh_port_setup(xesp_usbh_port_callback_t* callback)
{
    ESP_LOGI(TAG, "port setup");

    // register callback
    port_event_callback = callback;
    if(!port_event_callback){
        ESP_LOGE(TAG, "you must provide a callback");
        return NULL;
    }

    // create event queue
    port_evt_queue = xQueueCreate(PORT_EVENT_QUEUE_LEN, sizeof(event_msg_t));
    if(!port_evt_queue){
        ESP_LOGE(TAG, "failed to create port event queue");
        return NULL;
    }

    // create task
    bool success = xTaskCreate(port_event_task, "port_task", 4*1024, NULL, 10, NULL);
    if (!success){
        ESP_LOGE(TAG, "failed to create port task");
        return NULL;
    }

    //Install HCD
    hcd_config_t config = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    if(hcd_install(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Error to install HCD!");
         return NULL;
    }

    // port config
    hcd_port_config_t port_config = {
        .callback = port_isr_callback,
        .callback_arg = (void *)port_evt_queue,
        .context = NULL,
    };

    // initialize the port
    hcd_port_handle_t port_1 = NULL;
    esp_err_t err = hcd_port_init(XESP_USBH_PORT_COUNT, &port_config, &port_1);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "Error to init port: %d!", err);
        return NULL;
    }
    if(port_1 == NULL){
        ESP_LOGE(TAG, "Error to init port. null");
        return NULL;
    }

    // check port state
    hcd_port_state_t port_state = hcd_port_get_state(port_1);
    if(port_state == HCD_PORT_STATE_NOT_POWERED){
        ESP_LOGI(TAG, "USB host setup properly");
    }

    // Force disconnected state on PHY
    port_phy_force_conn_state(false, 0);    

    // power on
    err = hcd_port_command(port_1, HCD_PORT_CMD_POWER_ON);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "power on failed.");
        return NULL;
    }

    ESP_LOGI(TAG, "Port is power ON now");

    //allow connected state on PHY
    port_phy_force_conn_state(true, pdMS_TO_TICKS(10));

    return port_1;
}
