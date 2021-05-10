
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "hal/usbh_ll.h"
#include "hcd.h"

#include "esp_intr_alloc.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_err.h"

#include "usb_utils.h"
#include "xesp_usbh.h"

#include "xesp_usbh_xfer.h"

#define PIPE_EVENT_QUEUE_LEN         10

#define XESP_USB_MAX_SIMULTANEOUS_XFERS 2

static usb_irp_t irps[XESP_USB_MAX_SIMULTANEOUS_XFERS]; // the irps themselves

static uint8_t *irp_data_buffers[XESP_USB_MAX_SIMULTANEOUS_XFERS]; // the IO data

// keep track of which IRPS are free for use
static uint8_t irps_freelist_count;
static uint8_t irps_freelist[XESP_USB_MAX_SIMULTANEOUS_XFERS]; // the index of the irp that is free

// used by freertos waitBits
#define XFER_DONE_BIT 1 
#define XFER_ERROR_INVALID_BIT 4
#define XFER_ERROR_IRP_NOT_AVAIL_BIT 8
#define XFER_ERROR_OVERFLOW_BIT 16 
#define XFER_ERROR_XFER_BIT 32 

static EventGroupHandle_t irp_xfer_done_xEvents[XESP_USB_MAX_SIMULTANEOUS_XFERS];// notify when xfer done
static SemaphoreHandle_t irp_freelist_xMutex;
static SemaphoreHandle_t irp_counting_xSemaphore;
static SemaphoreHandle_t irp_enqueue_xSemaphore;

typedef struct {
    hcd_port_handle_t port;
    hcd_pipe_handle_t pipe;
    hcd_pipe_event_t pipe_event;
} pipe_event_msg_t;

static TaskHandle_t pipe_task_handle = NULL;
static QueueHandle_t pipe_evt_queue;

// forward declaration
bool xesp_usbh_allocate_irps();

static const char * TAG = "xesp_usb_xfer";

/////////////////////////////////////////
// Callbacks
//

static bool pipe_isr_callback(hcd_pipe_handle_t pipe, 
                          hcd_pipe_event_t pipe_event,
                          void *user_arg, 
                          bool in_isr)
{
    pipe_event_msg_t msg = {
        .port = (hcd_pipe_handle_t)user_arg,
        .pipe = pipe,
        .pipe_event = pipe_event,
    };
    if (in_isr) {
        BaseType_t xTaskWoken = pdFALSE;
        xQueueSendFromISR(pipe_evt_queue, &msg, &xTaskWoken);
        return (xTaskWoken == pdTRUE);
    } else {
        xQueueSend(pipe_evt_queue, &msg, portMAX_DELAY);
        return false;
    }
}


static void pipe_event_task(void* unused)
{
    ESP_LOGI(TAG, "started pipe event task");

    pipe_evt_queue = xQueueCreate(PIPE_EVENT_QUEUE_LEN, sizeof(pipe_event_msg_t));

    while(1){
        pipe_event_msg_t msg;

        xQueueReceive(pipe_evt_queue, &msg, portMAX_DELAY);

        //ESP_LOGI(TAG, "pipe: %p event: %s", msg.pipe, hcd_pipe_event_str(msg.pipe_event));

        usb_irp_t *irp = hcd_irp_dequeue(msg.pipe);

        if(irp == NULL){
            ESP_LOGE(TAG, "pipe event task: irp null");
            continue;
        }

        switch (msg.pipe_event)
        {
            case HCD_PIPE_EVENT_IRP_DONE:
                break;
            case HCD_PIPE_EVENT_ERROR_IRP_NOT_AVAIL:
            case HCD_PIPE_EVENT_ERROR_OVERFLOW:
                ESP_LOGE(TAG, "%s pipe: %p", hcd_pipe_event_str(msg.pipe_event), msg.pipe);
                break;
            case HCD_PIPE_EVENT_NONE:
            case HCD_PIPE_EVENT_ERROR_XFER:
            case HCD_PIPE_EVENT_INVALID:
            case HCD_PIPE_EVENT_ERROR_STALL:
                ESP_LOGE(TAG, "Ressetting pipe. %s pipe: %p", hcd_pipe_event_str(msg.pipe_event), msg.pipe);
                hcd_pipe_command(msg.pipe, HCD_PIPE_CMD_RESET);
                break;
        }

        uint16_t irp_idx = irp - &irps[0];
        xEventGroupSetBits(irp_xfer_done_xEvents[irp_idx], (uint32_t) msg.pipe_event);

        //ESP_LOGI(TAG, "irp: %u set bits", irp_idx);
    }
}


/////////////////////////////////
// Init
//

void xesp_usbh_xfer_init()
{
    // allocate irps
    bool irp_alloc = xesp_usbh_allocate_irps();
    if (irp_alloc == false){
        ESP_LOGE(TAG, "could not allocate IRPs");
        // should PDASSERT...
    }

   irp_counting_xSemaphore = xSemaphoreCreateCounting(XESP_USB_MAX_SIMULTANEOUS_XFERS, XESP_USB_MAX_SIMULTANEOUS_XFERS);
   if( irp_counting_xSemaphore == NULL ){
        ESP_LOGE(TAG, "could not create irp counting xSemaphore");
        // should PDASSERT...
   }

   irp_enqueue_xSemaphore = xSemaphoreCreateMutex();
   if( irp_enqueue_xSemaphore == NULL ){
        ESP_LOGE(TAG, "could not create irp enqueue xSemaphore");
        // should PDASSERT...
   }

   irp_freelist_xMutex = xSemaphoreCreateMutex();
   if( irp_freelist_xMutex == NULL ){
        ESP_LOGE(TAG, "could not create irp freelist xMutex");
        // should PDASSERT...
   }

   for(int i = 0; i < XESP_USB_MAX_SIMULTANEOUS_XFERS; i++){
        irp_xfer_done_xEvents[i] = xEventGroupCreate();
        if( irp_xfer_done_xEvents[i] == NULL ){
            ESP_LOGE(TAG, "could not create xfer done xEvent");
            // should PDASSERT...
        }
   }

    // start task if needed
    if (pipe_task_handle == NULL){
        ESP_LOGI(TAG, "starting pipe task");
        bool sucess = xTaskCreate(pipe_event_task, "pipe_task", 4*1024, (void *) 1, 5, &pipe_task_handle);

        if (!sucess) {
            ESP_LOGE(TAG, "could not start pipe task");
            // should PDASSERT...
        }
    }
}


// free irps
void xesp_usbh_xfer_deinit()
{
    // free irps
    ESP_LOGI(TAG, "Freeing IRPs\n");
    for (int i = 0; i < XESP_USB_MAX_SIMULTANEOUS_XFERS; i++) {
        heap_caps_free(irp_data_buffers[i]);
    }

    // stop pipe task
    vTaskDelete(pipe_task_handle);
}

bool xesp_usbh_allocate_irps(){

    ESP_LOGI(TAG, "allocating IRPS");

    for (int i = 0; i < XESP_USB_MAX_SIMULTANEOUS_XFERS; i++) {

        // data buffers
        size_t length = sizeof(usb_ctrl_req_t) + XESP_USB_MAX_XFER_BYTES;
        irp_data_buffers[i] = heap_caps_calloc(1, length, MALLOC_CAP_DMA);
        if(NULL == irp_data_buffers[i]){
            ESP_LOGE(TAG,"alloc irp data buffer failed.");
            for (int k = 0; k < i; k++) { // free prev iterations
                heap_caps_free(irp_data_buffers[k]); 
            } 
            return false;
        }

        //Initialize IRP and IRP list
        irps[i].data_buffer = irp_data_buffers[i];
        irps[i].num_iso_packets = 0;
        irps[i].num_bytes = XESP_USB_MAX_XFER_BYTES; // worst case max packet size
    }

    // initialize freelist
    for (int i = 0; i < XESP_USB_MAX_SIMULTANEOUS_XFERS; i++) {
        irps_freelist[i] = i;
    }
    irps_freelist_count = XESP_USB_MAX_SIMULTANEOUS_XFERS;

    return true;
}

/////////////////////////////////
// Endpoints
//

hcd_pipe_handle_t xesp_usbh_xfer_open_endpoint(hcd_port_handle_t port, uint8_t device_addr, usb_desc_ep_t* ep)
{
    ESP_LOGI(TAG, "open endpoint");

    // Esspressif doesn't support hubs yet. 
    // Therefore, getting the speed of a port implicitly determines the speed of the device.
    usb_speed_t port_speed;
    if(ESP_OK != hcd_port_get_speed(port, &port_speed)){
        ESP_LOGE(TAG, "could not get port speed. port: %p", port);
    }

    // control endpoint?
    bool use_ep0 = ep == NULL || ep->bEndpointAddress == 0;

    if (use_ep0) {
        ESP_LOGI(TAG, "CONTROL endpoint EP0");
    } else {
        usb_util_print_ep(ep);
    }

    hcd_pipe_config_t config = {
        .callback = pipe_isr_callback,
        .callback_arg = (void *)port,
        .context = NULL,
        .ep_desc = use_ep0 ? NULL : ep, // null signals ep0 (control)
        .dev_addr = device_addr,
        .dev_speed = port_speed,
    };

    hcd_pipe_handle_t pipe = NULL;
    esp_err_t err = hcd_pipe_alloc(port, &config, &pipe);

    if(err != ESP_OK){
        ESP_LOGE(TAG, "cant alloc pipe");
        return NULL;
    }
    if(pipe == NULL) {
        ESP_LOGE(TAG, "pipe is NULL, err: %i", err);
        return NULL;
    }

    ESP_LOGI(TAG, "pipe alloc'd %p", pipe);

    return pipe;
}

bool xesp_usbh_xfer_close_endpoint(hcd_pipe_handle_t pipe){

    // prevent additional enqueues
    xSemaphoreTake(irp_enqueue_xSemaphore, portMAX_DELAY);

    //Dequeue transfer requests
    do{
        usb_irp_t *irp = hcd_irp_dequeue(pipe);
        if(irp == NULL) break;
    }while(1);

    xSemaphoreGive(irp_enqueue_xSemaphore);

    //Delete the pipe
    if(ESP_OK != hcd_pipe_free(pipe)) {
        ESP_LOGE(TAG, "err to free pipes");
        return false;
    }

    return true;
}


/////////////////////////////////
// IRPs
//


uint8_t xesp_usbh_xfer_irp_idx(usb_irp_t* irp){
    // determine the index of the irp
    return irp - &irps[0];
}


// blocks until an irp is available
usb_irp_t* xesp_usbh_xfer_take_irp(){
    // counting semaphore - ensures we can only have X simultaneous transfers
    xSemaphoreTake(irp_counting_xSemaphore, portMAX_DELAY);

    // at this point, 1 or more irp are available

    // mutex - only 1 thread can be accessing the freelist
    xSemaphoreTake(irp_freelist_xMutex, portMAX_DELAY);

    uint8_t idx = irps_freelist[irps_freelist_count - 1]; 
    irps_freelist_count -= 1;

    // make sure these are reset each time
    usb_irp_t* irp = &irps[idx];

    memset(irp, 0, sizeof(usb_irp_t)); // clear

    irp->actual_num_bytes = 0;
    irp->num_bytes = XESP_USB_MAX_XFER_BYTES; //1 worst case MPS
    irp->data_buffer = irp_data_buffers[idx];
    irp->num_iso_packets = 0;

    memset(irp->data_buffer, 0, XESP_USB_MAX_XFER_BYTES);

    //ESP_LOGI(TAG, "took irp %u (idx). free: %u (count)", idx, irps_freelist_count);

    xSemaphoreGive(irp_freelist_xMutex);
    return irp;
}

// mark irp as available
void xesp_usbh_xfer_give_irp(usb_irp_t* irp){

    uint16_t idx = irp - &irps[0];

    //ESP_LOGI(TAG,"returning irp %u", idx);

    xSemaphoreTake(irp_freelist_xMutex, portMAX_DELAY); // freelist

    irps_freelist_count++;
    irps_freelist[irps_freelist_count - 1] = idx;

    //ESP_LOGI(TAG,"returned irp %u (idx). free: %u (count)", idx, irps_freelist_count);
    xSemaphoreGive(irp_freelist_xMutex); // freelist

    // mark an irp as available 
    xSemaphoreGive(irp_counting_xSemaphore);
}

// transfer
hcd_pipe_event_t xesp_usbh_xfer_irp(hcd_pipe_handle_t pipe, usb_irp_t* irp){

    // this semaphore makes sure only 1 thread enqueues at a time,
    // so that we can empty the queue when needed by aquiring this semaphore
    xSemaphoreTake(irp_enqueue_xSemaphore, portMAX_DELAY);

    uint16_t idx = irp - &irps[0];

    // debug
    //ESP_LOGI(TAG,"enqueued xfer irp %u. waiting.", idx);
    //usb_util_print_irp(&irps[idx]);

    //Enqueue the transfer request
    esp_err_t err;
    if(ESP_OK != (err = hcd_irp_enqueue(pipe, &irps[idx]))) {
        ESP_LOGE(TAG, "xfer irp enqueue error: %d - %s", err, esp_err_to_name(err));
        xSemaphoreGive(irp_enqueue_xSemaphore);
        return false;
    }

    xSemaphoreGive(irp_enqueue_xSemaphore);

    ESP_LOGI(TAG,"enqueued.");

    EventBits_t uxBits = 0;

    while(!uxBits) {
        uxBits = xEventGroupWaitBits(
                irp_xfer_done_xEvents[idx],   /* The event group being tested. */
                0x00FFFFFF,           /* The bits within the event group to wait for. */
                pdTRUE,        /* clear the bits after 'wait' completes */
                pdFALSE,       /* Wait for all bits? */
                portMAX_DELAY); 

        // ^ portMAX_DELAY is requred for now. 
        // For some reason if we allow timeouts, then the next time we 
        // get an event we crash on hcd_irp_dequeue
        if (!uxBits) { // timeout
            usb_irp_t *irp2 = hcd_irp_dequeue(pipe);
            hcd_pipe_state_t pipe_state = hcd_pipe_get_state(pipe);
            ESP_LOGE(TAG, "xfer timeout irp:%u pipe: %p pipe_state: %s %s", idx,
                pipe, hcd_pipe_state_str(pipe_state), irp2 ? "IRP Dequeued" : "No more IRPs");
            return HCD_PIPE_EVENT_ERROR_XFER;
        }
    }

    hcd_pipe_event_t event = (hcd_pipe_event_t) uxBits;

    if (event != HCD_PIPE_EVENT_IRP_DONE){
        hcd_pipe_state_t state = hcd_pipe_get_state(pipe);
        ESP_LOGE(TAG, "xfer irp:%u pipe: %p pipe_state: %s error %s", idx,
            pipe, hcd_pipe_state_str(state), hcd_pipe_event_str(event));
    } else {
        ESP_LOGI(TAG, "irp %i xfer done", idx);
    }

    return event;
}

