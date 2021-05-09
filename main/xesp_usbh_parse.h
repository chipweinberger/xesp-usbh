#pragma once

#include "hcd.h"
#include "usb.h"

#include "xesp_usbh.h"

//////////////////////////////
// Parse
//

// ALLOCATES! Must be freed with 'xesp_usbh_parse_free_config'
xesp_usb_config_descriptor_t* xesp_usbh_parse_config(uint8_t *data, uint32_t length);

// Free
void xesp_usbh_parse_free_config(xesp_usb_config_descriptor_t* descriptor);


////////////////////////////
// Print
// 

void xesp_usbh_print_interface_descriptor(xesp_usb_interface_descriptor_t* interface_desc);

void xesp_usbh_print_interface(xesp_usb_interface_t* interface);

void xesp_usbh_print_config_descriptor(xesp_usb_config_descriptor_t* config_desc);                                       






