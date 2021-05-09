
#include "stdlib.h"
#include "stdint.h"

#include "esp_err.h"
#include "esp_log.h"

#include "usb.h"

#include "usb_utils.h"


static const char* TAG = "USB utils";

// forward declaration, class specific printers
usb_cs_interface_printer_func* find_cs_interface_printer(uint8_t bDeviceClass, uint8_t bDeviceSubClass);
usb_cs_endpoint_printer_func* find_cs_endpoint_printer(uint8_t bDeviceClass, uint8_t bDeviceSubClass);


static inline int bcd_to_decimal(unsigned char x) {
    return x - 6 * (x >> 4);
}

void utf16_to_utf8(char* in, char* out, uint8_t len){
    for (size_t i = 0; i < len; i++){
        out[i/2] = in[i];
        i++;
    }
}

///////////////////////////////////////////
// HCD Enums
//

const char* hcd_pipe_state_str(hcd_pipe_state_t state){
    switch (state){
    case HCD_PIPE_STATE_ACTIVE: return "active";
    case HCD_PIPE_STATE_HALTED: return "halted";
    case HCD_PIPE_STATE_INVALID: return "invalid";
    }
    return "Unknown";
}

const char* hcd_port_event_str(hcd_port_event_t event){
    switch (event){
    case HCD_PORT_EVENT_NONE: return "None";
    case HCD_PORT_EVENT_CONNECTION: return "Connected";
    case HCD_PORT_EVENT_DISCONNECTION: return "Disconnected";
    case HCD_PORT_EVENT_ERROR: return "Error";
    case HCD_PORT_EVENT_OVERCURRENT: return "Overcurrent";
    case HCD_PORT_EVENT_SUDDEN_DISCONN: return "Sudden Disconnection";
    }
    return "Unknown";
}

const char* hcd_pipe_event_str(hcd_pipe_event_t event){
    switch (event){
    case HCD_PIPE_EVENT_NONE: return "None";
    case HCD_PIPE_EVENT_IRP_DONE: return "IRP done";
    case HCD_PIPE_EVENT_INVALID: return "Invalid";
    case HCD_PIPE_EVENT_ERROR_XFER: return "Xfer Error";
    case HCD_PIPE_EVENT_ERROR_IRP_NOT_AVAIL: return "IRP Not Available Error";
    case HCD_PIPE_EVENT_ERROR_OVERFLOW: return "Overflow Error";
    case HCD_PIPE_EVENT_ERROR_STALL: return "Stall Error";
    }
    return "Unknown";
}

const char* hcd_port_cmd_str(hcd_port_cmd_t cmd){
    switch (cmd){
    case HCD_PORT_CMD_POWER_ON: return "Power ON";
    case HCD_PORT_CMD_POWER_OFF: return "Power OFF";
    case HCD_PORT_CMD_RESET: return "Reset";
    case HCD_PORT_CMD_SUSPEND: return "Suspend";
    case HCD_PORT_CMD_RESUME: return "Resume";
    case HCD_PORT_CMD_DISABLE: return "Disable";
    }
    return "Unknown";
}

const char* hcd_pipe_cmd_str(hcd_pipe_cmd_t cmd){
    switch (cmd){
    case HCD_PIPE_CMD_ABORT: return "Abort";
    case HCD_PIPE_CMD_RESET: return "Reset";
    case HCD_PIPE_CMD_CLEAR: return "Clear";
    case HCD_PIPE_CMD_HALT: return "Halt";
    }
    return "Unknown";
}

///////////////////////////////////////////
// USB Enums
//

const char* usb_descriptor_type_str(uint8_t bDescriptorType){
    switch (bDescriptorType){
    case USB_W_VALUE_DT_DEVICE: return "Device";
    case USB_W_VALUE_DT_CONFIG: return "Config";
    case USB_W_VALUE_DT_STRING: return "String";
    case USB_W_VALUE_DT_INTERFACE: return "Interface";
    case USB_W_VALUE_DT_ENDPOINT: return "Endpoint";
    case USB_W_VALUE_DT_DEVICE_QUALIFIER: return "Device Qualifier";
    case USB_W_VALUE_DT_OTHER_SPEED_CONFIG: return "Other Speed Config";
    case USB_W_VALUE_DT_INTERFACE_POWER: return "Interface Power";
    case USB_W_VALUE_DT_CS_INTERFACE: return "CS Interface";
    case USB_W_VALUE_DT_CS_ENDPOINT: return "CS Endpoint";
    }
    return "Unknown";
}

const char* usb_transfer_status_str(usb_transfer_status_t status){
    switch (status){
    case USB_TRANSFER_STATUS_COMPLETED: return "Completed";
    case USB_TRANSFER_STATUS_ERROR: return "Error";
    case USB_TRANSFER_STATUS_TIMED_OUT: return "Timed Out";
    case USB_TRANSFER_STATUS_CANCELLED: return "Cancelled";
    case USB_TRANSFER_STATUS_STALL: return "Stall";
    case USB_TRANSFER_STATUS_NO_DEVICE: return "No Device";
    case USB_TRANSFER_STATUS_OVERFLOW: return "Overflow";
    }
    return "Unknown";
}

uint8_t usb_ep_number(uint8_t bEndpointAddress){
    uint8_t addr_bits = 0b00000111;
    return bEndpointAddress & addr_bits;
}

const char* usb_ep_direction_str(uint8_t bEndpointAddress){
    uint8_t dir_bit = 0b10000000;
    if(bEndpointAddress & dir_bit){
        return "In";
    } else {
        return "Out";
    }
}

const char* usb_ep_transfer_type_str(uint8_t bmAttributes){
    uint8_t type_bits = 0b00000011;
    switch(bmAttributes & type_bits){
        case 0: return "Control";
        case 1: return "Iso";
        case 2: return "Bulk";
        case 3: return "Interrupt";
        default: return "Invalid";
    }
}


///////////////////////////////////////////
// USB Descriptions
//

static char* usb_bDeviceClass_str(uint8_t bDeviceClass){
    switch (bDeviceClass){
        case USB_CLASS_PER_INTERFACE: return "Generic Device";
        case USB_CLASS_AUDIO: return "Audio";
        case USB_CLASS_COMM: return "CDC Communications";
        case USB_CLASS_HID: return "HID";
        case USB_CLASS_PHYSICAL: return "Physical";
        case USB_CLASS_STILL_IMAGE: return "Image";
        case USB_CLASS_PRINTER: return "Printer";
        case USB_CLASS_MASS_STORAGE: return "Mass Storage";
        case USB_CLASS_HUB: return "Hub";
        case USB_CLASS_CDC_DATA: return "CDC-data";
        case USB_CLASS_CSCID: return "Smart card";
        case USB_CLASS_CONTENT_SEC: return "Content security";
        case USB_CLASS_VIDEO: return "Video";
        case USB_CLASS_PERSONAL_HEALTHCARE: return "Personal heathcare";
        case USB_CLASS_AUDIO_VIDEO: return "Audio/Video devices";
        case USB_CLASS_BILLBOARD: return "Bilboard";
        case USB_CLASS_USB_TYPE_C_BRIDGE: return "USB-C bridge";
        case USB_CLASS_DIAGNOSTIC_DEVICE: return "Diagnostic device";
        case USB_CLASS_WIRELESS_CONTROLLER: return "Wireless controller";
        case USB_CLASS_MISC: return "Miscellaneous";
        case USB_CLASS_APP_SPEC: return "Application specific";
        case USB_CLASS_VENDOR_SPEC: return "Vendor specific";
    }
    return "Wrong class";
}

static char* usb_bDeviceSubClass_str(uint8_t bDeviceClass, uint8_t bDeviceSubClass){
    if (bDeviceClass == 0x01){ // Audio
        switch (bDeviceSubClass){
        case USB_SUBCLASS_Audio_Undefined: return "Undefined";
        case USB_SUBCLASS_Audio_Control: return "Audio Control";
        case USB_SUBCLASS_Audio_Streaming: return "Audio Streaming";
        case USB_SUBCLASS_Audio_Midi_Streaming: return "Midi Streaming";
        }
        return "Wrong subclass";
    } else {
        return "";
    }
}

void usb_util_print_description(uint8_t* data, 
                                uint32_t length, 
                                uint8_t bDeviceClass, 
                                uint8_t bDeviceSubClass){
    if (!data) {printf("desc: NULL\n"); return;}
    uint32_t offset = 0;
    uint8_t* ptr = data + offset;
    // all descriptions have these 2 fields
    uint8_t bLength = *(ptr + 0);
    uint8_t bDescriptorType = *(ptr + 1);
    do{
        ESP_LOGI(TAG, "type: %d, off: %u, bLength:%u len: %u\n", bDescriptorType, offset, bLength, length);
        switch (bDescriptorType) {
        case USB_W_VALUE_DT_DEVICE: usb_util_print_devc((usb_desc_devc_t2*)ptr); break;
        case USB_W_VALUE_DT_CONFIG: usb_util_print_cfg((usb_desc_cfg_t*)ptr); break;
        case USB_W_VALUE_DT_STRING: usb_util_print_str((usb_desc_str_t*)ptr); break;
        case USB_W_VALUE_DT_INTERFACE: usb_util_print_intf((usb_desc_intf_t*)ptr); break;
        case USB_W_VALUE_DT_ENDPOINT: usb_util_print_ep((usb_desc_ep_t*)ptr); break;
        // class specified
        case USB_W_VALUE_DT_CS_INTERFACE: 
            usb_util_print_cs_intf(bDeviceClass, bDeviceSubClass, ptr, bLength); 
            break;
        case USB_W_VALUE_DT_CS_ENDPOINT: 
            usb_util_print_cs_ep(bDeviceClass, bDeviceSubClass, ptr, bLength); 
            break;
        default: 
            printf("unknown descriptor: %u", bDescriptorType); 
            break;
        }
        offset += bLength;
        if(offset >= length){
            break;
        }
        printf("\n more data within same packet..\n");
        ptr = data + offset;
        bLength = *(ptr + 0);
        bDescriptorType = *(ptr + 1);
    }while(1);
}

// device  
void usb_util_print_devc(usb_desc_devc_t2* data){
    printf("\nDevice descriptor:\n");
    if (!data) {printf("NULL\n"); return;}
    printf("Length: %d\n", data->bLength);
    printf("Descriptor type: %x (%s)\n", data->bDescriptorType, usb_descriptor_type_str(data->bDescriptorType));
    printf("USB version: %d.%02d\n", bcd_to_decimal(data->bcdUSB >> 8), bcd_to_decimal(data->bcdUSB & 0xff));
    printf("Device class: 0x%02x (%s)\n", data->bDeviceClass, usb_bDeviceClass_str(data->bDeviceClass));
    printf("Device subclass: 0x%02x (%s)\n", data->bDeviceSubClass, usb_bDeviceSubClass_str(data->bDeviceClass, data->bDeviceSubClass));
    printf("Device protocol: 0x%02x\n", data->bDeviceProtocol);
    printf("EP0 max packet size: %d\n", data->bMaxPacketSize0);
    printf("VID: 0x%04x\n", data->idVendor);
    printf("PID: 0x%04x\n", data->idProduct);
    printf("Revision number: %d.%02d\n", bcd_to_decimal(data->bcdDevice >> 8), bcd_to_decimal(data->bcdDevice & 0xff));
    printf("Manufacturer id: %d\n", data->iManufacturer);
    printf("Product id: %d\n", data->iProduct);
    printf("Serial id: %d\n", data->iSerialNumber);
    printf("Configurations num: %d\n", data->bNumConfigurations);
}

// configuration 
void usb_util_print_cfg(usb_desc_cfg_t* data){
    printf("\nConfig:\n");
    if (!data) {printf("NULL\n"); return;}
    printf("Length: %d\n", data->bLength);
    printf("Descriptor type: %x (%s)\n", data->bDescriptorType, usb_descriptor_type_str(data->bDescriptorType));
    printf("Total Length: %d\n", data->wTotalLength);
    printf("Number of Interfaces: %d\n", data->bNumInterfaces);
    printf("Configuration Num: %d\n", data->bConfigurationValue);
    printf("iConfiguration (string): %d\n", data->iConfiguration);
    printf("Attributes: 0x%02x\n", data->bmAttributes);
    printf("Max power: %d mA\n", data->bMaxPower * 2);
}

// interface 
void usb_util_print_intf(usb_desc_intf_t* data){
    printf("\nInterface:\n");
    if (!data) {printf("NULL\n"); return;}
    printf("Length: %d\n", data->bLength);
    printf("Descriptor type: %x (%s)\n", data->bDescriptorType, usb_descriptor_type_str(data->bDescriptorType));
    printf("bInterfaceNumber: %d\n", data->bInterfaceNumber);
    printf("bAlternateSetting: %d\n", data->bAlternateSetting);
    printf("bNumEndpoints: %d\n", data->bNumEndpoints);
    printf("bInterfaceClass: 0x%02x (%s)\n", data->bInterfaceClass, usb_bDeviceClass_str(data->bInterfaceClass));
    printf("bInterfaceSubClass: 0x%02x (%s)\n", data->bInterfaceSubClass, usb_bDeviceSubClass_str(data->bInterfaceClass, data->bInterfaceSubClass));
    printf("bInterfaceProtocol: 0x%02x\n", data->bInterfaceProtocol);
}

// endpoint 
void usb_util_print_ep(usb_desc_ep_t* data){
    printf("\nEndpoint:\n");
    if (!data) {printf("NULL\n"); return;}
    printf("Length: %d\n", data->bLength);
    printf("Descriptor type: %x (%s)\n", data->bDescriptorType, usb_descriptor_type_str(data->bDescriptorType));
    printf("bEndpointAddress: 0x%02x (addr: %u Dir: %s)\n", data->bEndpointAddress,
        usb_ep_number(data->bEndpointAddress), usb_ep_direction_str(data->bEndpointAddress));
    printf("bmAttributes: 0x%02x (%s)\n", data->bmAttributes,
        usb_ep_transfer_type_str(data->bmAttributes));
    printf("bDescriptorType: %d\n", data->bDescriptorType);
    printf("wMaxPacketSize: %d\n", data->wMaxPacketSize);
    printf("bInterval: %d ms\n", data->bInterval);
}

// string 
void usb_util_print_str(usb_desc_str_t* data){
    if (!data) {printf("NULL\n"); return;}
    char* str = (char*)calloc(1, data->bLength);
    utf16_to_utf8((char*)&data->val[2], str, data->bLength);
    printf("strings: %s\n", str);
    free(str);
}

// class specified interface
void usb_util_print_cs_intf(uint8_t bDeviceClass, uint8_t bDeviceSubClass, uint8_t* data, uint32_t length){

    printf("\nInterface (Class Specified):\n");
    if (!data) {printf("NULL\n"); return;}
    printf("Length: %d\n", *(data));
    printf("Descriptor type: %x (%s)\n", *(data + 1), usb_descriptor_type_str(*(data + 1)));
    printf("Device class: 0x%02x (%s)\n", bDeviceClass, usb_bDeviceClass_str(bDeviceClass));
    printf("Device subclass: 0x%02x (%s)\n", bDeviceSubClass, usb_bDeviceSubClass_str(bDeviceClass, bDeviceSubClass));

    usb_cs_interface_printer_func* func = find_cs_interface_printer(bDeviceClass, bDeviceSubClass);
    if (func){
        func(data, length);
    } else {
        printf("no printer for cs interface\n");
    }
}

// class specified endpoint
void usb_util_print_cs_ep(uint8_t bInterfaceClass, uint8_t bInterfaceSubClass, uint8_t* data, uint32_t length){

    printf("\nEndpoint (Class Specified):\n");
    if (!data) {printf("NULL\n"); return;}
    printf("Length: %d\n", *(data));
    printf("Descriptor type: %x (%s)\n", *(data + 1), usb_descriptor_type_str(*(data + 1)));
    printf("bInterfaceClass: 0x%02x\n", bInterfaceClass);
    printf("bInterfaceSubClass: 0x%02x\n", bInterfaceSubClass);

    usb_cs_endpoint_printer_func* func = find_cs_endpoint_printer(bInterfaceClass, bInterfaceSubClass);
    if (func){
        func(data, length);
    } else {
        printf("no printer for cs endpoint\n");
    }
}

/////////////////////////////////////////////////
//  irp
//

// print irp 
void usb_util_print_irp(usb_irp_t* irp) {
    printf("\nIRP:\n");
    if (!irp) {printf("NULL\n"); return;}
    //printf("tailq_entry:\n");
    printf("reserved ptr: %p\n", irp->reserved_ptr);
    printf("reserved flags: %u\n", irp->reserved_flags);
    printf("data buffer: %p\n", irp->data_buffer);
    printf("num bytes: %u\n", irp->num_bytes);
    printf("actual num bytes: %u\n", irp->actual_num_bytes);
    printf("status: %s\n",  usb_transfer_status_str(irp->status));
    printf("timeout: %u (millis)\n", irp->timeout);
    printf("context: %p\n", irp->context);
    printf("num iso packets: %d\n", irp->num_iso_packets);
}

/////////////////////////////////////////////////
//  Class specific interface / endpooint printers
//

// keep this small as needed for perf.
// If we try to register more than this, we'll log errors to the console
#define MAX_CS_PRINTERS 2 

// We keep an array of function pointers and 
// loop through them to find right bDeviceClass

// class specified interface printer
struct cs_interface_printer_t {
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    usb_cs_interface_printer_func* cs_printer;
};

struct cs_endpoint_printer_t {
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    usb_cs_endpoint_printer_func* cs_printer;
};

typedef struct cs_interface_printer_t cs_interface_printer_t;
typedef struct cs_endpoint_printer_t cs_endpoint_printer_t;

// each time we register a cs printer we increment these
uint8_t registered_cs_interface_count = 0;
uint8_t registered_cs_endpoint_count = 0;

cs_interface_printer_t cs_interface_printers[MAX_CS_PRINTERS];
cs_endpoint_printer_t cs_endpoint_printers[MAX_CS_PRINTERS];


usb_cs_interface_printer_func* find_cs_interface_printer(uint8_t bDeviceClass, uint8_t bDeviceSubClass) {
    for (int i = 0; i < MAX_CS_PRINTERS; i++){
        cs_interface_printer_t* p = &cs_interface_printers[i];
        if (p->bDeviceClass == bDeviceClass &&
            p->bDeviceSubClass == bDeviceSubClass){
            return p->cs_printer;
        }
    }
    return NULL;
}

usb_cs_endpoint_printer_func* find_cs_endpoint_printer(uint8_t bInterfaceClass, uint8_t bInterfaceSubClass) {
    for (int i = 0; i < MAX_CS_PRINTERS; i++){
        cs_endpoint_printer_t* p = &cs_endpoint_printers[i];
        if (p->bInterfaceClass == bInterfaceClass &&
            p->bInterfaceSubClass == bInterfaceSubClass){
            return p->cs_printer;
        }
    }
    return NULL;
}

void usb_util_register_cs_interface_printer(uint8_t bDeviceClass, 
                                            uint8_t bDeviceSubClass, 
                                            usb_cs_interface_printer_func* cs_printer)
{
    if (registered_cs_interface_count == MAX_CS_PRINTERS){
        ESP_LOGE(TAG, "Cannot register class-specified interface printer. hit MAX_CS_PRINTERS");
        return;
    }

    // register
    uint8_t idx = registered_cs_interface_count;
    cs_interface_printers[idx].bDeviceClass = bDeviceClass;
    cs_interface_printers[idx].bDeviceSubClass = bDeviceSubClass;
    cs_interface_printers[idx].cs_printer = cs_printer;

    registered_cs_interface_count++;
}

void usb_util_register_cs_endpoint_printer(uint8_t bInterfaceClass, 
                                            uint8_t bInterfaceSubClass,  
                                            usb_cs_endpoint_printer_func* cs_printer)
{
    if (registered_cs_endpoint_count == MAX_CS_PRINTERS){
        ESP_LOGE(TAG, "Cannot register class-specified endpoint printer. hit MAX_CS_PRINTERS");
        return;
    }

    // register
    uint8_t idx = registered_cs_endpoint_count;
    cs_endpoint_printers[idx].bInterfaceClass = bInterfaceClass;
    cs_endpoint_printers[idx].bInterfaceSubClass = bInterfaceSubClass;
    cs_endpoint_printers[idx].cs_printer = cs_printer;

    registered_cs_endpoint_count++;
}



