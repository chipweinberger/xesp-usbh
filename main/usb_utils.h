
#pragma once

#include "stdlib.h"
#include "stdint.h"

#include "hcd.h"

#include "usb.h"

///////////////////////////////////////////
// USB Specification
//

// These bDescriptorType's are not defined by espressif for some reason
#define USB_W_VALUE_DT_CS_INTERFACE         0x24 // Class specified interface 
#define USB_W_VALUE_DT_CS_ENDPOINT          0x25 // Class specified endpoint 

// These bDeviceClass's are not defined by espressif for some reason
#define USB_CLASS_DIAGNOSTIC_DEVICE 0xdc
#define USB_CLASS_WIRELESS_CONTROLLER 0xe0

// USB Device Audio Subclasses - bDeviceSubClass
#define USB_SUBCLASS_Audio_Undefined 0x00
#define USB_SUBCLASS_Audio_Control 0x01
#define USB_SUBCLASS_Audio_Streaming 0x02
#define USB_SUBCLASS_Audio_Midi_Streaming 0x03

struct usb_desc_devc_t2{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
};

typedef struct usb_desc_devc_t2 usb_desc_devc_t2;

#define USB_CTRL_REQ_INIT_GET_STRING(ctrl_req_ptr, lang, desc_index, len) ({ \
    (ctrl_req_ptr)->bRequestType = USB_B_REQUEST_TYPE_DIR_IN | USB_B_REQUEST_TYPE_TYPE_STANDARD | USB_B_REQUEST_TYPE_RECIP_DEVICE;   \
    (ctrl_req_ptr)->bRequest = USB_B_REQUEST_GET_DESCRIPTOR;   \
    (ctrl_req_ptr)->wValue = (USB_W_VALUE_DT_STRING << 8) | ((desc_index) & 0xFF); \
    (ctrl_req_ptr)->wIndex = (lang);    \
    (ctrl_req_ptr)->wLength = (len);  \
})

void utf16_to_utf8(char* in, char* out, uint8_t len);

///////////////////////////////////////////
// HCD Enums
//

const char* hcd_pipe_state_str(hcd_pipe_state_t state);
const char* hcd_port_event_str(hcd_port_event_t event);
const char* hcd_pipe_event_str(hcd_pipe_event_t event);
const char* hcd_port_cmd_str(hcd_port_cmd_t cmd);
const char* hcd_pipe_cmd_str(hcd_pipe_cmd_t cmd);

///////////////////////////////////////////
// USB Enums
//

const char* usb_descriptor_type_str(uint8_t bDescriptorType);
const char* usb_transfer_status_str(usb_transfer_status_t status);

///////////////////////////////////////////
// USB Descriptions
//

// print any type of description
void usb_util_print_description(uint8_t* data, 
                                uint32_t length, 
                                uint8_t bDeviceClass, 
                                uint8_t bDeviceSubClass);

// print device  
void usb_util_print_devc(usb_desc_devc_t2* obj);

// print configuration 
void usb_util_print_cfg(usb_desc_cfg_t* obj);

// print interface 
void usb_util_print_intf(usb_desc_intf_t* obj);

// print endpoint or cs_endpoint 
void usb_util_print_ep(usb_desc_ep_t* obj);

// print string 
void usb_util_print_str(usb_desc_str_t* obj);

/////////////////////////////////////////////////
//  irp
//

// print string 
void usb_util_print_irp(usb_irp_t* irp);

/////////////////////////////////////////////////
//  Class specified descriptors
//

// bDescriptorType == 0x24, are "class specific" interfaces.
// bDescriptorType == 0x25, are "class specific" endpoints.
// i.e. The interface / endpoint format depends on the device bDeviceClass.
// We can register handlers for these here.

// class specified interface
void usb_util_print_cs_intf(uint8_t bDeviceClass, 
                            uint8_t bDeviceSubClass, 
                            uint8_t* data, 
                            uint32_t length);

// class specified endpoint
void usb_util_print_cs_ep(uint8_t bInterfaceClass, 
                          uint8_t bInterfaceSubClass, 
                          uint8_t* data, 
                          uint32_t length);

typedef int usb_cs_interface_printer_func(uint8_t* data, size_t length);
typedef int usb_cs_endpoint_printer_func(uint8_t* data, size_t length);

// register class specified interface printer
void usb_util_register_cs_interface_printer(uint8_t bDeviceClass, 
                                            uint8_t bDeviceSubClass, 
                                            usb_cs_interface_printer_func* cs_printer);

// register class specified endpoint printer 
void usb_util_register_cs_endpoint_printer(uint8_t bInterfaceClass, 
                                            uint8_t bInterfaceSubClass,
                                            usb_cs_endpoint_printer_func* cs_printer);



