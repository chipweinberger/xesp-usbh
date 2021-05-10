#pragma once

#include "hcd.h"
#include "usb.h"

#include "xesp_usbh.h"

/*

All the code in this file has to do with interacting 
with USB endpoints (i.e. pipes in HCD parlance)

    - opening and closing endpoints
    - transfering data to / from endpoints using irps

WARNING: Do not use this code directly. You should use xesp_usbh.h which wraps these.

*/

/////////////////////////////////
// Init
//

// allocate IRPs, init pipe task, etc
void xesp_usbh_xfer_init();

// free IRPs, delete pipe task, etc
void xesp_usbh_xfer_deinit();

/////////////////////////////////
// Endpoints
//

// open an endpoint
hcd_pipe_handle_t xesp_usbh_xfer_open_endpoint(hcd_port_handle_t port, uint8_t device_addr, usb_desc_ep_t* ep);

// close an endpoint
bool xesp_usbh_xfer_close_endpoint(hcd_pipe_handle_t pipe);


/////////////////////////////////
// IRPs
//

// blocks until an irp is available
usb_irp_t* xesp_usbh_xfer_take_irp();

// mark irp as available
void xesp_usbh_xfer_give_irp(usb_irp_t*);

// blocks until the irp is completed. return HCD_PIPE_EVENT_IRP_DONE on success
hcd_pipe_event_t  xesp_usbh_xfer_irp(hcd_pipe_handle_t pipe, usb_irp_t* irp);

// for logging
uint8_t xesp_usbh_xfer_irp_idx(usb_irp_t*);

