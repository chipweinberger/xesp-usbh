#pragma once

#include "usb.h"
#include "hcd.h"


#define XESP_USBH_PORT_COUNT 1 // we have only 1 port on the hardware

#define XUSB_OK HCD_PIPE_EVENT_IRP_DONE

#define XESP_USB_MAX_XFER_BYTES 256

//////////////////////////////
// Definitions
//

struct xesp_usb_device_t{
    hcd_port_handle_t port;
    hcd_pipe_handle_t ctrl_pipe;
};

typedef struct xesp_usb_device_t xesp_usb_device_t;

struct xesp_usb_endpoint_descriptor_t{
    usb_desc_ep_t val; // the endpoint descriptor
    // class specified endpoints follow after the main endpoint descriptor ('val')
    // and are stored here in 'extras' if you want to parse them.
    uint8_t* extras; 
    uint32_t extras_length;
};

typedef struct xesp_usb_endpoint_descriptor_t xesp_usb_endpoint_descriptor_t;


struct xesp_usb_interface_descriptor_t{
    usb_desc_intf_t val; // the interface descriptor
    // an interface contains the endpoints 
    xesp_usb_endpoint_descriptor_t** endpoints;// array of enpoints
    uint16_t endpoint_count;
    // class specified interfaces follow after the main interface descriptor ('val')
    // and are stored here in 'extras' if you want to parse them.
    uint8_t* extras; 
    uint32_t extras_length;
};

typedef struct xesp_usb_interface_descriptor_t xesp_usb_interface_descriptor_t;

// The USB Spec has a notion of "Alternate" Interfaces.
// For example, there might be 3 interfaces, 'bInterfaceNum' 1 through 3, for a given configuration.
// But bInterfaceNum #2 can actually be a collection of different interfaces signifying 
// different modes of operation for that interface.
// These different modes of operation for interface #2 are called alt settings.
// They each have the same bInterfaceNum (#2), but a different bAlternateSetting.
// You can call "set_active_interface_alt_setting" to set currently active altSetting
// for interface #2.
struct xesp_usb_interface_t{
    xesp_usb_interface_descriptor_t** altSettings; // array of interface descriptors in this interface
    uint16_t altSettings_count; // typically 1, meaning no actual alternate settings
};

typedef struct xesp_usb_interface_t xesp_usb_interface_t;

// forward declaration
struct x_malloc_pool_t;

// This struct contains *all* the descriptors in a given configuration. 
// Implementation Details: In the USB Spec, there is no way to get a interface or endpoint descriptor.
// You can only ask for the config descriptor and it will send *all* the descriptors 
// in that config at once.
struct xesp_usb_config_descriptor_t{
    usb_desc_cfg_t val; // the config descriptor
    xesp_usb_device_t device; // for convenience
    xesp_usb_interface_t** interfaces; // array of interfaces
    uint16_t interface_count;

    // used internally for memory management.
    struct x_malloc_pool_t* _malloc_pool;
};

typedef struct xesp_usb_config_descriptor_t xesp_usb_config_descriptor_t;

typedef struct usb_desc_device_t usb_desc_device_t;