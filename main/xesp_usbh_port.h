#pragma once

#include "hcd.h"

// handle port events
typedef void(xesp_usbh_port_callback_t)(hcd_port_handle_t port, hcd_port_event_t event);

// start port task
hcd_port_handle_t xesp_usbh_port_setup(xesp_usbh_port_callback_t* callback);
