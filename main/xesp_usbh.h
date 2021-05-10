#pragma once

#include "usb.h"
#include "hcd.h"

#include "usb_utils.h"

#include "xesp_usbh_defs.h"
#include "xesp_usbh_parse.h"

/////////////////////////////////
// Init
//

// init buffers, FreeRTOS objects, etc
void xesp_usbh_init();


//////////////////////////////////
// Device 
//

// Blocks until we open the next connected USB device.
// Multiple thread can wait on this next device, and all threads
// will open the connected device simultaneously.
//
// open_count is an in / out parameter that we use to keep track
// of which connection events you have already been informed of. It should start at 0.
xesp_usb_device_t xesp_usbh_open_device(uint64_t* open_count);

bool xesp_usbh_close_device(xesp_usb_device_t device);

//////////////////////////////////
// Endpoints
//

hcd_pipe_handle_t xesp_usbh_open_endpoint(xesp_usb_device_t device, usb_desc_ep_t* ep);

bool xesp_usbh_close_endpoint(hcd_pipe_handle_t pipe);

hcd_pipe_event_t xesp_usbh_xfer_to_pipe(hcd_pipe_handle_t pipe, uint8_t* data, uint16_t length);

hcd_pipe_event_t xesp_usbh_xfer_from_pipe(hcd_pipe_handle_t pipe, uint8_t* data, uint16_t* num_bytes_transfered);

//////////////////////////////////
// Descriptors 
//

// get device descriptor. 
// returns HCD_PIPE_EVENT_IRP_DONE on success.
hcd_pipe_event_t xesp_usbh_get_device_descriptor(xesp_usb_device_t device, usb_desc_devc_t2* descriptor);

// get Nth config descriptor (...and the interface / endpoints within that config).
// returns HCD_PIPE_EVENT_IRP_DONE on success.
// ALLOCATION! The caller must free this object.
hcd_pipe_event_t xesp_usbh_get_config_descriptor(xesp_usb_device_t device, 
                                                 uint8_t config_idx,
                                                 xesp_usb_config_descriptor_t** config);

// Free
void xesp_usbh_free_config_descriptor(xesp_usb_config_descriptor_t* descriptor);


// get Nth string descriptor
// ALLOCATION! The caller must free the returned str object.
hcd_pipe_event_t xesp_usbh_get_string_descriptor(xesp_usb_device_t device, 
                                                 uint8_t string_idx,
                                                char** str);


//////////////////////////////////
//  Set
//

// In the USB spec, the host must assign each device an address after opening the control port
hcd_pipe_event_t xesp_usbh_set_addr(xesp_usb_device_t device, 
                                   uint8_t addr);

// set the currently active configuration
hcd_pipe_event_t xesp_usbh_set_config(xesp_usb_device_t device, 
                                      uint8_t config_idx);

// Set Active Interface Alt Setting.
// see docs in xesp_usb_interface_t.
void xesp_usbh_set_active_interface_alt_setting(xesp_usb_interface_t* interface, 
                                               int alternate_setting);
