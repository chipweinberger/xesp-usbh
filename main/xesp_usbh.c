






#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_intr_alloc.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_log.h"

#include "hal/usbh_ll.h"
#include "hcd.h"

#include "usb_utils.h"

#include "xesp_usbh_port.h"
#include "xesp_usbh_xfer.h"
#include "xesp_usbh_parse.h"
#include "xesp_usbh.h"

#define XESP_USBH_MAX_PIPES 8

static const char* TAG = "xesp usb";

// we keep track of all open pipe information here
struct xesp_open_pipe_t{
    hcd_port_handle_t port;
    hcd_pipe_handle_t pipe;
    int16_t device_addr; // usb device address
    uint64_t nth_open; // the nth pipe opened
    bool is_control_pipe; // is the main control endpoint (EP0) ?
    uint16_t bMaxPacketSize0; // obtained from the device description
};

typedef struct xesp_open_pipe_t xesp_open_pipe_t;

// event fires when device is connected
static EventGroupHandle_t connect_xEvent;
#define CONNECT_BITS 1

// global counter that increases every time a pipe is opened
static uint64_t num_ctlr_pipes_opened = 0;

// all the currently open pipes
static xesp_open_pipe_t open_pipes[XESP_USBH_MAX_PIPES];

static SemaphoreHandle_t open_pipes_mutex;


// port callback 
static void port_event_callback(hcd_port_handle_t port, hcd_port_event_t event){

    if (event == HCD_PORT_EVENT_ERROR ||
        event == HCD_PORT_EVENT_OVERCURRENT) {
        ESP_LOGE(TAG, "port %p event %s!", port, hcd_port_event_str(event));
    }

    if (event == HCD_PORT_EVENT_DISCONNECTION ||
        event == HCD_PORT_EVENT_SUDDEN_DISCONN) {
        ESP_LOGI(TAG, "port %p event %s!", port, hcd_port_event_str(event));
    }


    // loop through all devices and get the control pipe
    hcd_port_handle_t control_pipe = NULL;
    xSemaphoreTake(open_pipes_mutex, portMAX_DELAY);
    for(int i = 0; i < XESP_USBH_MAX_PIPES; i++){
        if (open_pipes[i].port == port &&
            open_pipes[i].is_control_pipe){
            control_pipe = open_pipes[i].pipe;
        }
    }
    xSemaphoreGive(open_pipes_mutex);

    hcd_port_state_t port_state = hcd_port_get_state(port);
    hcd_pipe_state_t pipe_state = HCD_PIPE_STATE_INVALID;

    xesp_usb_device_t device;
    device.port = port;
    device.ctrl_pipe = NULL; // not used

    hcd_port_handle_event(port);

    switch (event){
        case HCD_PORT_EVENT_NONE: ESP_LOGI(TAG, "port nont %p", port); break;
        case HCD_PORT_EVENT_ERROR: ESP_LOGI(TAG, "port error %p", port); break;
        case HCD_PORT_EVENT_OVERCURRENT: ESP_LOGI(TAG, "port overcurrent %p", port); break;
        case HCD_PORT_EVENT_DISCONNECTION:
            ESP_LOGI(TAG, "port disconnection %p", port);
            xesp_usbh_close_device(device);
            hcd_port_command(port, HCD_PORT_CMD_POWER_OFF); 
            break;
        case HCD_PORT_EVENT_SUDDEN_DISCONN: 
            ESP_LOGI(TAG, "port sudden disconnection %p", port);
            hcd_port_command(port, HCD_PORT_CMD_RESET);

            if (control_pipe){
                pipe_state = hcd_pipe_get_state(control_pipe);
            }    

            if (pipe_state == HCD_PIPE_STATE_INVALID) {     

                ESP_LOGW(TAG, "pipe state: %s", hcd_pipe_state_str(pipe_state));

                // calls hcd_pipe_free on all open pipes
                xesp_usbh_close_device(device);

                esp_err_t err;
                if(port_state == HCD_PORT_STATE_RECOVERY){
                    err = hcd_port_recover(port);
                    if(err != ESP_OK ){
                        port_state = hcd_port_get_state(port);
                        ESP_LOGE(TAG, "recovery - should be not powered state %d => (%d)", port_state, err);
                    }
                } else {
                    port_state = hcd_port_get_state(port);
                    ESP_LOGE(TAG, "hcd_port_state_t: %d", port_state);
                }
                err = hcd_port_command(port, HCD_PORT_CMD_POWER_ON);
                if(err == ESP_OK) {
                    ESP_LOGI(TAG, "Port powered ON");
                }
            }
            break;
        case HCD_PORT_EVENT_CONNECTION:
            ESP_LOGI(TAG, "port connected %p", port);
            if(port_state == HCD_PORT_STATE_DISABLED){
                ESP_LOGI(TAG, "HCD_PORT_STATE_DISABLED");
            } 
            if(ESP_OK == hcd_port_command(port, HCD_PORT_CMD_RESET)) {
                ESP_LOGI(TAG, "USB device reset");
            }
            port_state = hcd_port_get_state(port); // get new state
            if(port_state == HCD_PORT_STATE_ENABLED){
                xesp_usbh_open_endpoint(device, NULL); // open the control pipe (EP0)
                ESP_LOGI(TAG, "xEventGroupSetBits CONNECT_BITS");
                xEventGroupSetBits(connect_xEvent, CONNECT_BITS);
            }
            break;
    }  
}

void xesp_usbh_init(){

    // connect event 
    connect_xEvent = xEventGroupCreate();
    if (!connect_xEvent){
        ESP_LOGE(TAG, "Could not init usbh. connect_xEvent failed");
        //PDAASSERT(g_connect_event_group, "this must not fail");
    }

    // usb pipes mutex
    open_pipes_mutex = xSemaphoreCreateMutex();
    if(open_pipes_mutex == NULL ){
        ESP_LOGE(TAG, "could not create usb pipes mutex");
        // should PDASSERT...
    }

    hcd_port_handle_t port = xesp_usbh_port_setup(&port_event_callback);
    if(!port) {
        ESP_LOGE(TAG, "Could not init usbh");
        // should PDASSERT...
        return;
    }

    // init pipe
    xesp_usbh_xfer_init();
}


//////////////////////////////////
// Devices 
//

xesp_usb_device_t xesp_usbh_open_device(uint64_t* open_idx){

    // the device / ctrl pipe we will return
    xesp_open_pipe_t found;

    found.port = NULL;

    uint32_t loop_count = 0;

    while (!found.port) { 

        if (loop_count > 0) {
            // If we programmed our logic correctly, we should
            // always have a device ready to be opened after the first loop and should never hit this
            ESP_LOGW(TAG, "No device ready to be opened. odd. blocking more (loop count %i)...", loop_count); 
        }

        loop_count++;

        // has caller already opened all devices?
        if (*open_idx == num_ctlr_pipes_opened) {

            // block until a device is connected
            ESP_LOGI(TAG, "Waiting for USB Connection...");

            xEventGroupWaitBits(
                connect_xEvent, 
                CONNECT_BITS, /* The bits within the event group to wait for. */
                true,        /* clear the bits after 'wait' completes */
                false,       /* Wait for all bits? */
                portMAX_DELAY );
        }

        ESP_LOGI(TAG, "USB Connection Found.");

        // at this point xesp_usbh_open_endpoint was called elsewhere
        // so EP0 should be availble in the open_pipes global object

        xSemaphoreTake(open_pipes_mutex, portMAX_DELAY);

        // loop through all open pipes and return the first that the callers hasn't opened yet
        for(int i = 0; i < XESP_USBH_MAX_PIPES; i++){

            // find the next device for the caller to open
            if (*open_idx < open_pipes[i].nth_open &&
                open_pipes[i].is_control_pipe ) {

                found = open_pipes[i];
                break;
            }
        }

        xSemaphoreGive(open_pipes_mutex);
    }

    // to return 
    xesp_usb_device_t device;
    device.port = found.port;
    device.ctrl_pipe = found.pipe;

    // return the open idx of this device
    *open_idx = found.nth_open;

    ESP_LOGI(TAG, "opened device #%llu port %p ctrl_pipe %p", *open_idx, device.port, device.ctrl_pipe);

    return device;
}

// calls hcd_pipe_free on all open pipes of this device
bool xesp_usbh_close_device(xesp_usb_device_t device) {

    xSemaphoreTake(open_pipes_mutex, portMAX_DELAY);

    // close all pipes belonging to a device
    for (int i = 0; i < XESP_USBH_MAX_PIPES; i++){
        
        // is this pipe part of the given device?
        if (open_pipes[i].port == device.port) {

            // close the pipe
            bool success = xesp_usbh_xfer_close_endpoint(open_pipes[i].pipe);
            if (!success) {
                ESP_LOGE(TAG, "failed to close pipe %p control_pipe? %s", open_pipes[i].pipe, 
                    open_pipes[i].is_control_pipe ? "YES" : "NO");
                xSemaphoreGive(open_pipes_mutex);
                return false;
            }

            ESP_LOGI(TAG, "closed pipe %p control_pipe? %s", open_pipes[i].pipe, 
                open_pipes[i].is_control_pipe ? "YES" : "NO");

            open_pipes[i].port = NULL;
            open_pipes[i].pipe = NULL;
            open_pipes[i].nth_open = 0;
            open_pipes[i].device_addr = 0;
            open_pipes[i].is_control_pipe = false;
            open_pipes[i].bMaxPacketSize0 = 0;
        }
    }

    xSemaphoreGive(open_pipes_mutex);
    return true;
}

//////////////////////////////////
// Endpoints 
//

xesp_open_pipe_t* new_xesp_usbh_get_pipe_info(hcd_pipe_handle_t pipe){
    for (int i = 0; i < XESP_USBH_MAX_PIPES; i++){
        if (open_pipes[i].pipe == pipe){
            return &open_pipes[i];
        }
    }
    ESP_LOGE(TAG, "could not find pipe info for pipe %p", pipe);
    return NULL;
}

hcd_pipe_handle_t xesp_usbh_open_endpoint(xesp_usb_device_t device, usb_desc_ep_t* ep){

    if (ep == NULL){
        ESP_LOGI(TAG, "opening control pipe");
    }

    // determine the device address
    xesp_open_pipe_t* ctrl_info = new_xesp_usbh_get_pipe_info(device.ctrl_pipe);
    if (!ctrl_info) {
        ESP_LOGE(TAG, "could not find pipe info for pipe %p", device.ctrl_pipe);
        return NULL;
    }

    uint8_t device_addr = ctrl_info->device_addr;
    if (device_addr == (uint8_t) -1){
        ESP_LOGE(TAG, "cant open pipe invalid device_addr -1");
        return NULL;
    } else {
        printf("device_addr %u\n", device_addr);
    }

    // open the pipe / endpoint
    hcd_pipe_handle_t pipe = xesp_usbh_xfer_open_endpoint(device.port, device_addr, ep);

    if (!pipe) {
        ESP_LOGE(TAG, "failed to open control pipe");
        return NULL;
    }

    xSemaphoreTake(open_pipes_mutex, portMAX_DELAY);

    // keep track of this pipe in the open_pipes gobal object
    xesp_open_pipe_t* free = NULL;
    xesp_open_pipe_t* control_pipe = NULL;
    for (int i = 0; i < XESP_USBH_MAX_PIPES; i++){
        // find a free slot
        if (open_pipes[i].port == NULL) {
            free = &open_pipes[i];
        } 
        // if we are not the control pipe, find it
        if (ep != NULL && 
            open_pipes[i].port == device.port &&
            open_pipes[i].is_control_pipe) {
            control_pipe = &open_pipes[i];
        }
    }

    if (free == NULL){
        ESP_LOGE(TAG, "failed to open pipe. At max pipe capacity.");
        xSemaphoreGive(open_pipes_mutex);
        return NULL;
    }

    bool is_control_pipe = ep == NULL || ep->bEndpointAddress == 0;

    // open pipe counter
    if(is_control_pipe) {
        num_ctlr_pipes_opened++;
    }

    ESP_LOGI(TAG, "num_ctlr_pipes_opened: %llu", num_ctlr_pipes_opened);

    // assign the pipe to the free slot
    free->port = device.port;
    free->pipe = pipe;
    free->nth_open = num_ctlr_pipes_opened;
    free->is_control_pipe = is_control_pipe;
    free->device_addr = device_addr;

    if (control_pipe) {
        free->bMaxPacketSize0 = control_pipe->bMaxPacketSize0;
    } else {
        // this gets filled in when the user first asks for the device descriptor
        free->bMaxPacketSize0 = 0; 
    }

    hcd_pipe_state_t pipe_state = hcd_pipe_get_state(pipe);
    ESP_LOGI(TAG, "(open) pipe state: %s", hcd_pipe_state_str(pipe_state));

    esp_err_t rc = hcd_pipe_command(pipe, HCD_PIPE_CMD_RESET);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG,"pipe reset failed");
    }
    ESP_LOGI(TAG, "(after reset) pipe state: %s", hcd_pipe_state_str(pipe_state));

    xSemaphoreGive(open_pipes_mutex);

    return pipe;
}

bool xesp_usbh_close_endpoint(hcd_pipe_handle_t pipe){

    xSemaphoreTake(open_pipes_mutex, portMAX_DELAY);

    bool success = xesp_usbh_xfer_close_endpoint(pipe);
    if (!success) {
        ESP_LOGE(TAG, "failed to close pipe. hcd error");
        xSemaphoreGive(open_pipes_mutex);
        return false;
    }

    bool found = false;
    bool is_control_pipe = false;
    for (int i = 0; i < XESP_USBH_MAX_PIPES; i++){
        // clear this pipe
        if (open_pipes[i].pipe == pipe) {
            is_control_pipe = open_pipes[i].is_control_pipe;
            open_pipes[i].port = NULL;
            open_pipes[i].pipe = NULL;
            open_pipes[i].nth_open = 0;
            open_pipes[i].is_control_pipe = false;
            open_pipes[i].device_addr = 0;
            open_pipes[i].bMaxPacketSize0 = 0;
            found = true;
        }
    }

    if (!found){
        ESP_LOGE(TAG, "failed to close pipe. not found in registry");
        xSemaphoreGive(open_pipes_mutex);
        return false;
    }

    ESP_LOGI(TAG, "closed pipe %p control_pipe? %s", pipe, is_control_pipe ? "YES" : "NO");

    xSemaphoreGive(open_pipes_mutex);
    return true;
}

hcd_pipe_event_t xesp_usbh_xfer_to_pipe(hcd_pipe_handle_t pipe, uint8_t* data, uint16_t length){
    return 0;
}

hcd_pipe_event_t xesp_usbh_xfer_from_pipe(hcd_pipe_handle_t pipe, 
                                          uint8_t* data, 
                                          uint16_t* num_bytes_transfered){
    // blocks until an irp is available
    usb_irp_t* irp = xesp_usbh_xfer_take_irp();

    ESP_LOGI(TAG, "xfer from pipe: %p", pipe);

    irp->num_bytes = 64;// smaller numbers cause crashes at time of writing... odd.

    // blocks until the irp is completed.
    hcd_pipe_event_t rc = xesp_usbh_xfer_irp(pipe, irp);

    if (rc == XUSB_OK){
        ESP_LOGI(TAG, "actual bytes transfered: %u", irp->actual_num_bytes);

        *num_bytes_transfered = irp->actual_num_bytes;

        uint8_t * data_returned = irp->data_buffer;// + sizeof(usb_ctrl_req_t);
        memcpy(data, data_returned, irp->actual_num_bytes); 
    } else {
        *num_bytes_transfered = 0;
    }

    // mark irp as available
    xesp_usbh_xfer_give_irp(irp);

    return rc;
}

//////////////////////////////////
// Descriptors 
//

hcd_pipe_event_t xesp_usbh_get_device_descriptor(xesp_usb_device_t device, usb_desc_devc_t2* desc){

    // blocks until an irp is available
    usb_irp_t* irp = xesp_usbh_xfer_take_irp();

    ESP_LOGI(TAG, "get device description port %p pipe %p irp %u",
         device.port, device.ctrl_pipe, xesp_usbh_xfer_irp_idx(irp));

    USB_CTRL_REQ_INIT_GET_DEVC_DESC((usb_ctrl_req_t *) irp->data_buffer);
    irp->num_bytes = 64;

    // blocks until the irp is completed.
    hcd_pipe_event_t rc = xesp_usbh_xfer_irp(device.ctrl_pipe, irp);

    if (rc == XUSB_OK){
        // the data buffer always begins with the ctrl request struct
        uint8_t * data_returned = irp->data_buffer + sizeof(usb_ctrl_req_t);
        *desc = *(usb_desc_devc_t2*) data_returned;
    }

    // mark irp as available
    xesp_usbh_xfer_give_irp(irp);

    if (rc == XUSB_OK){
        // set the max packet size
        xSemaphoreTake(open_pipes_mutex, portMAX_DELAY);
        for(int i = 0; i < XESP_USBH_MAX_PIPES; i++){
            if (open_pipes[i].port == device.port) {
                ESP_LOGI(TAG, "port: %p bMaxPacketSize0: %u", device.port, desc->bMaxPacketSize0);
                open_pipes[i].bMaxPacketSize0 = desc->bMaxPacketSize0;
            }
        }
        xSemaphoreGive(open_pipes_mutex);
    }

    return rc;
}


hcd_pipe_event_t xesp_usbh_get_config_descriptor(xesp_usb_device_t device, 
                                                 uint8_t config_idx,
                                                 xesp_usb_config_descriptor_t** config){

    // blocks until an irp is available
    usb_irp_t* irp = xesp_usbh_xfer_take_irp();

    ESP_LOGI(TAG, "get config description port %p pipe %p irp %u", 
        device.port, device.ctrl_pipe, xesp_usbh_xfer_irp_idx(irp));

    USB_CTRL_REQ_INIT_GET_CFG_DESC((usb_ctrl_req_t *) irp->data_buffer, 1, XESP_USB_MAX_XFER_BYTES);
    //important!! if is shorter than buffer and descriptor is longer than num_bytes, then it will stuck here
    // so its best if both values are equal
    irp->num_bytes = XESP_USB_MAX_XFER_BYTES;

    // blocks until the irp is completed.
    hcd_pipe_event_t rc = xesp_usbh_xfer_irp(device.ctrl_pipe, irp);

    if (rc == XUSB_OK){
        ESP_LOGI(TAG, "actual bytes transfered: %u", irp->actual_num_bytes);

        // the data buffer always begins with the ctrl request struct
        uint8_t * data_returned = irp->data_buffer + sizeof(usb_ctrl_req_t);

        // parse all the config data
        *config = xesp_usbh_parse_config(data_returned, irp->actual_num_bytes);
    }

    // mark irp as available
    xesp_usbh_xfer_give_irp(irp);

    return XUSB_OK;
}

void xesp_usbh_free_config_descriptor(xesp_usb_config_descriptor_t* config){
    // ^this func is just a wrapper
    xesp_usbh_parse_free_config(config);
}

hcd_pipe_event_t xesp_usbh_get_string_descriptor(xesp_usb_device_t device, 
                                                 uint8_t string_idx,
                                                char** str)
{
    // blocks until an irp is available
    usb_irp_t* irp = xesp_usbh_xfer_take_irp();

    ESP_LOGI(TAG, "get str %u port %p pipe %p irp %u", string_idx, 
        device.port, device.ctrl_pipe, xesp_usbh_xfer_irp_idx(irp));

    const int ENGLISH = 0;
    USB_CTRL_REQ_INIT_GET_STRING((usb_ctrl_req_t *) irp->data_buffer, ENGLISH, string_idx, XESP_USB_MAX_XFER_BYTES);
    irp->num_bytes = XESP_USB_MAX_XFER_BYTES;
    
    // blocks until the irp is completed.
    hcd_pipe_event_t rc = xesp_usbh_xfer_irp(device.ctrl_pipe, irp);

    if (rc == XUSB_OK){
        // the data buffer always begins with the ctrl request struct
        uint8_t * data_returned = irp->data_buffer + sizeof(usb_ctrl_req_t);
        usb_desc_str_t* desc = (usb_desc_str_t*) data_returned;

        // first 2 bytes are USB length and USB type
        uint16_t len = irp->actual_num_bytes - 2;

        // copy to out str
        *str = calloc(1, len + 1); // guarentee null terminated
        utf16_to_utf8((char*) &desc->val[2], *str, len);
    }

    // mark irp as available
    xesp_usbh_xfer_give_irp(irp);

    return rc;
}

//////////////////////////////////
// Set addr 
//

// set usb address of device, if needed
hcd_pipe_event_t xesp_usb_set_addr_auto(xesp_usb_device_t device){

    xSemaphoreTake(open_pipes_mutex, portMAX_DELAY);

    xesp_open_pipe_t* found = NULL;
    bool taken_addrs[XESP_USBH_MAX_PIPES] = {0};

    // loop through all devices and determine available addresses
    for(int i = 0; i < XESP_USBH_MAX_PIPES; i++){
        int16_t addr = open_pipes[i].device_addr;
        if (addr != -1 && open_pipes[i].port != NULL) {
            taken_addrs[addr] = true;
        }

        if (device.port == open_pipes[i].port) {
            found = &open_pipes[i];
        }
    }

    // we already have a USB address
    if (found->device_addr != 0) {
        xSemaphoreGive(open_pipes_mutex);
        return XUSB_OK;
    }

    // determine an available USB address, if needed
    int16_t new_addr = -1;
    for (int i = 0; i < XESP_USBH_MAX_PIPES; i++){
        if (taken_addrs[i] == false) {
            // note: we do this assignment within the open_pipes mutex
            // so that only one thread later does the assignment
            found->device_addr = i;
            new_addr = i;
            break;
        }
    }

    if (new_addr == -1) {
        // This should really never happen because there are 128 available device addresses. Thats a lot.
        // If we hit this, its probably a bug somewhere.
        ESP_LOGE(TAG, "could not set device addr. no available USB address"); 
        return HCD_PIPE_STATE_INVALID;
    }

    hcd_pipe_event_t rc = xesp_usbh_set_addr(device, new_addr);
    if (rc != XUSB_OK) {
        ESP_LOGE(TAG, "usb set addr failed"); 
        found->device_addr = 0;
    }

    xSemaphoreGive(open_pipes_mutex);

    return rc;
}

hcd_pipe_event_t xesp_usbh_set_addr(xesp_usb_device_t device, uint8_t addr){

    // blocks until an irp is available
    usb_irp_t* irp = xesp_usbh_xfer_take_irp();

    ESP_LOGI(TAG, "set addr: %u port: %p pipe: %p", addr, device.port, device.ctrl_pipe);

    xesp_open_pipe_t* info = new_xesp_usbh_get_pipe_info(device.ctrl_pipe);

    USB_CTRL_REQ_INIT_SET_ADDR((usb_ctrl_req_t *) irp->data_buffer, addr);

    // set addr is special in that it *needs* this to be zero
    irp->num_bytes = 0;

    // blocks until the irp is completed.
    hcd_pipe_event_t rc = xesp_usbh_xfer_irp(device.ctrl_pipe, irp);

    // mark irp as available
    xesp_usbh_xfer_give_irp(irp);

    if(ESP_OK != hcd_pipe_update(device.ctrl_pipe, addr, info->bMaxPacketSize0)) {
        ESP_LOGE(TAG, "failed to update ctrl pipe addr");
    } else {
        info->device_addr = addr; // update the address
        ESP_LOGI(TAG, "hcd_pipe_update pipe %p addr %u bMaxPacketSize0 %u", 
            device.ctrl_pipe, addr, info->bMaxPacketSize0);
    }

    return rc;
}

hcd_pipe_event_t xesp_usbh_set_config(xesp_usb_device_t device, 
                                      uint8_t config_idx)
{

    // blocks until an irp is available
    usb_irp_t* irp = xesp_usbh_xfer_take_irp();

    ESP_LOGI(TAG, "set config: %u port: %p pipe: %p", config_idx, device.port, device.ctrl_pipe);

    USB_CTRL_REQ_INIT_SET_CONFIG((usb_ctrl_req_t *) irp->data_buffer, config_idx);

    // set config is special in that it *needs* this to be zero
    irp->num_bytes = 0;

    // blocks until the irp is completed.
    hcd_pipe_event_t rc = xesp_usbh_xfer_irp(device.ctrl_pipe, irp);

    // mark irp as available
    xesp_usbh_xfer_give_irp(irp);

    return rc;
}
