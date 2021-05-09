
#include "string.h"

#include "usb_utils.h"

#include "esp_log.h"

#include "xesp_usbh_parse.h"

static const char* TAG = "usb parse";

//////////////////////////////
// Memory Pool
//
#define POOL_PTRS_PER_NODE 10

struct x_malloc_pool_node_t;

// Parsing config descriptors is really annoying 
// because of how much we need to malloc. So much is dynamic sized.
// To make things easier to 'free', we keep track of all the memory we
// allocated in a linked list. Then we can just free everything at once.
struct x_malloc_pool_node_t {
    // store up to 10 at a time
    void* malloc_ptrs[POOL_PTRS_PER_NODE];
    uint8_t ptr_count;// ^ # of malloc ptrs stores in the above array
    struct x_malloc_pool_node_t* next;
};

typedef struct x_malloc_pool_node_t x_malloc_pool_node_t;

// linked list
struct x_malloc_pool_t {
    x_malloc_pool_node_t* head;
    x_malloc_pool_node_t* end; // for convenient quick access
};

typedef struct x_malloc_pool_t x_malloc_pool_t;

static x_malloc_pool_t* malloc_pool_create(){
    x_malloc_pool_t* pool = calloc(1, sizeof(x_malloc_pool_t));
    pool->head = calloc(1, sizeof(x_malloc_pool_node_t));
    pool->end = pool->head;
    return pool;
}

// allocate some memory from the pool
static void* malloc_pool(x_malloc_pool_t* pool, size_t bytes, bool zero){

    x_malloc_pool_node_t* end = pool->end;

    if (end->ptr_count == POOL_PTRS_PER_NODE) {
        // allocate new node
        x_malloc_pool_node_t* node2 = calloc(1, sizeof(x_malloc_pool_node_t));
        end->next = node2;
        pool->end = node2;
        end = node2;
    }

    void* ptr = zero ? calloc(1,bytes) : malloc(bytes);

    end->malloc_ptrs[end->ptr_count] = ptr;
    end->ptr_count++;

    return ptr;
}

static void malloc_pool_free_all_ptrs(x_malloc_pool_t* pool){
    ESP_LOGI(TAG, "Free config pool");

    if (pool == NULL){
        return;
    }

    x_malloc_pool_node_t* curr = pool->head;

    while (curr != NULL) {  
        // free all ptrs in the node
        for (int i = 0; i < curr->ptr_count; i++) {
            free(curr->malloc_ptrs[i]);
        }
        x_malloc_pool_node_t* next = curr->next; 
        free(curr); // free the node
        curr = next;
    }
    free(pool);
}


//////////////////////////////
// Parse
//

xesp_usb_endpoint_descriptor_t* xesp_usbh_parse_endpoint(x_malloc_pool_t* pool, 
                                                         uint8_t *data, 
                                                         uint32_t length, 
                                                         size_t* offset){

    uint32_t bytes_left = length - (*offset);
    if(bytes_left < sizeof(usb_desc_ep_t)) {
        ESP_LOGE(TAG, "endpoint length expected %u, got %u", sizeof(usb_desc_ep_t), bytes_left);
        *offset = length;
        return NULL;
    }

    usb_desc_ep_t* ep = (usb_desc_ep_t*) (data + (*offset));

    usb_util_print_ep(ep);

    if (ep->bDescriptorType != USB_W_VALUE_DT_ENDPOINT) {
        const char* dType  = usb_descriptor_type_str(ep->bDescriptorType);
        ESP_LOGE(TAG, "We expected an interface. but found %s", dType);
        return NULL;
    }

    // allocation
    xesp_usb_endpoint_descriptor_t* xEndpoint = malloc_pool(pool, sizeof(xesp_usb_endpoint_descriptor_t), true);

    // copy endpoint to buffer we own
    memcpy(&xEndpoint->val, ep, sizeof(usb_desc_ep_t));

    // go to next after the leading endpoint
    *offset += ep->bLength;

    //
    // parse CS endpoint descriptors
    //
    uint32_t extras_length = 0;
    uint8_t* extras_start = (data + (*offset)); // remember this

    uint8_t bDescriptorType = *(&data[0] + (*offset) + 1);
    uint8_t bLength         = *(&data[0] + (*offset) + 0);

    while(bDescriptorType == USB_W_VALUE_DT_CS_ENDPOINT && *offset < length){

        printf("\nCS Endpoint:\n");
        printf("bLength: %u\n", bLength);

        *offset += bLength;
        extras_length += bLength;

        bDescriptorType = *(&data[0] + (*offset) + 1);
        bLength         = *(&data[0] + (*offset) + 0);

        const char* dType  = usb_descriptor_type_str(bDescriptorType);
        printf("offset %u length %u\n", *offset, length);
        printf("%s\n", dType);
    }

    // copy to xEndpoint
    if (extras_length) {
        xEndpoint->extras_length = extras_length;
        xEndpoint->extras = malloc_pool(pool, extras_length, false);
        memcpy(xEndpoint->extras, extras_start, extras_length);
    }

    return xEndpoint;
}

xesp_usb_interface_descriptor_t* xesp_usbh_parse_interface(x_malloc_pool_t* pool, 
                                                           uint8_t *data, 
                                                           uint32_t length, 
                                                           size_t* offset){

    uint32_t bytes_left = length - (*offset);
    if(bytes_left < sizeof(usb_desc_intf_t)) {
        ESP_LOGE(TAG, "interface length expected %u, got %u", sizeof(usb_desc_intf_t), bytes_left);
        *offset = length;
        return NULL;
    }

    usb_desc_intf_t* intf = (usb_desc_intf_t*) (data + (*offset));

    if (intf->bDescriptorType != USB_W_VALUE_DT_INTERFACE){
        const char* dType  = usb_descriptor_type_str(intf->bDescriptorType);
        ESP_LOGE(TAG, "We expected an interface. but found %s", dType);
        return NULL;
    }

    // allocation
    xesp_usb_interface_descriptor_t* xIntfDesc = malloc_pool(pool, 
                                                            sizeof(xesp_usb_interface_descriptor_t), 
                                                            true); // zero'd

    // for debugging
    usb_util_print_intf(intf);

    // copy interface to buffer we own
    memcpy(&xIntfDesc->val, intf, sizeof(usb_desc_intf_t));

    // go to next after the leading interface
    *offset += intf->bLength;

    //
    // parse CS interface descriptors
    //
    uint32_t extras_length = 0;
    uint8_t* extras_start = (data + (*offset)); // remember this

    uint8_t bDescriptorType = *(&data[0] + (*offset) + 1);
    uint8_t bLength = *(&data[0] + (*offset) + 0);

    while(bDescriptorType == USB_W_VALUE_DT_CS_INTERFACE && *offset < length){
        printf("\nCS Interface:\n");
        printf("bLength: %u\n", bLength);

        *offset += bLength;
        extras_length += bLength;

        bDescriptorType = *(&data[0] + (*offset) + 1);
        bLength         = *(&data[0] + (*offset) + 0);
    }

    // copy to xIntfDesc
    if (extras_length) {
        xIntfDesc->extras_length = extras_length;
        xIntfDesc->extras = malloc_pool(pool, extras_length, false);
        memcpy(xIntfDesc->extras, extras_start, extras_length);
    }

    //
    // parse endpooints
    //

    // allocate endpoint array
    xIntfDesc->endpoints = malloc_pool(pool, (intf->bNumEndpoints * sizeof(void*)), true);
    xIntfDesc->endpoint_count = 0; // starts as zero and we increment as we parse

    // parse the endpoints
    while(*offset < length && xIntfDesc->endpoint_count < intf->bNumEndpoints){
        uint8_t bDescriptorType = *(&data[0] + (*offset) + 1);
        switch (bDescriptorType)
        {
            case USB_W_VALUE_DT_ENDPOINT:{
                xesp_usb_endpoint_descriptor_t* endpoint = xesp_usbh_parse_endpoint(pool, data, length, offset);
                if (!endpoint) {
                    return NULL;
                }
                xIntfDesc->endpoints[xIntfDesc->endpoint_count] = endpoint;
                xIntfDesc->endpoint_count++;
                break;
            }
            default:{
                const char* dType  = usb_descriptor_type_str(bDescriptorType);
                ESP_LOGE(TAG, "Only expected endpoints. but found %s", dType);
                return NULL;
            }
        }
    }

    return xIntfDesc;
}

xesp_usb_interface_t** merge_alternate_interfaces(x_malloc_pool_t* pool, 
                                                 xesp_usb_interface_descriptor_t** interfaces,
                                                 size_t interface_count,
                                                 uint16_t* unique_interfaces_count)
{
    // determine the number of alternate interfaces of each bInterfaceNum
    uint32_t* alternate_counts = malloc_pool(pool, 
                                            (interface_count * sizeof(uint32_t)), 
                                            true); 

    for (int i = 0; i < interface_count; i++){
        uint16_t bInterfaceNum = interfaces[i]->val.bInterfaceNumber;
        alternate_counts[bInterfaceNum]++;
    }

    // calculate the size of the interface array we will return
    uint16_t unique_count = 0;
    for (int i = 0; i < interface_count; i++){

        if (alternate_counts[i] != 0) {
            unique_count++;
        }
    }

    // allocate unique interfaces array
    xesp_usb_interface_t** unique_interfaces = malloc_pool(pool, 
                                                          (unique_count * sizeof(void*)),
                                                          true);

    // create each unique interface 
    for (int iUnq = 0; iUnq < unique_count; iUnq++){

        xesp_usb_interface_t* unique = malloc_pool(pool,
                                                   sizeof(xesp_usb_interface_t), 
                                                   true);


        unique->altSettings_count = alternate_counts[iUnq];

        unique->altSettings = malloc_pool(pool, 
                                          (alternate_counts[iUnq] * sizeof(void*)),
                                          true);

        // fill in the array with interfaces with matching bInterfaceNum's
        uint32_t altIdx = 0;
        for(int jIn = 0; jIn < interface_count; jIn++){

            uint16_t bInterfaceNum = interfaces[jIn]->val.bInterfaceNumber;

            if (bInterfaceNum == iUnq) {

                // add it to the alt settings array
                unique->altSettings[altIdx] = interfaces[jIn];
                
                altIdx++;
            }
        }

        if (altIdx != alternate_counts[iUnq]){
            ESP_LOGE(TAG, "(merging) expected these to match. %u != %u", altIdx, alternate_counts[iUnq]);
        }

        // copy to output array
        unique_interfaces[iUnq] = unique;
    }

    // size of array we are returning
    *unique_interfaces_count = unique_count;

    return unique_interfaces;
}

// Count the total number of interfaces in the configuration
// including alternate interfaces of the same bInterfaceNum
uint32_t config_interface_count(uint8_t *data, uint32_t length){
    uint16_t interface_count = 0;
    uint32_t offset = 0;
    while(offset < length) {
        uint8_t bDescriptorType = *(&data[0] + offset + 1);
        uint8_t bLength         = *(&data[0] + offset + 0);
        switch (bDescriptorType){
            case USB_W_VALUE_DT_INTERFACE: interface_count++; break;
            default: break;
        }
        offset += bLength;
        if(offset >= length) {
            break;
        }
    }
    return interface_count;
}

xesp_usb_config_descriptor_t* xesp_usbh_parse_config(uint8_t *data, uint32_t length){

    if(length < sizeof(usb_desc_cfg_t)) {
        ESP_LOGE(TAG, "config length expected %u, got %u", sizeof(usb_desc_cfg_t), length);
        return NULL;
    }

    usb_desc_cfg_t* config = (usb_desc_cfg_t*) data;

    if (config->bDescriptorType != USB_W_VALUE_DT_CONFIG) {
        const char* dType  = usb_descriptor_type_str(config->bDescriptorType);
        ESP_LOGE(TAG, "We expected the config descriptor to be first. but found %s", dType);
        return NULL;
    }

    // for debugging
    usb_util_print_cfg(config);

    //
    // parse the interfaces
    //

    // use a pool to make freeing memory simple
    x_malloc_pool_t* pool =  malloc_pool_create();

    // allocate temporary interfaces array
    uint32_t interface_count = config_interface_count(data, length);

    xesp_usb_interface_descriptor_t** all_interfaces = malloc_pool(pool, 
                                                                  (interface_count * sizeof(void*)),
                                                                  true); // zero'd

    uint16_t parsed_interfaces = 0;
    uint32_t offset = sizeof(usb_desc_cfg_t);
    while(offset < length) {
        uint8_t bDescriptorType = *(&data[0] + offset + 1);
        uint8_t bLength         = *(&data[0] + offset + 0);
        switch (bDescriptorType)
        {
            case USB_W_VALUE_DT_INTERFACE:{
                xesp_usb_interface_descriptor_t* interface = xesp_usbh_parse_interface(pool, data, length, &offset);
                if (!interface) {
                    goto fail;
                }
                all_interfaces[parsed_interfaces] = interface;
                parsed_interfaces++;
                break;
            }
            default:{
                const char* dType  = usb_descriptor_type_str(bDescriptorType);
                ESP_LOGE(TAG, "Only expected interfaces. but found %x (%s)", bDescriptorType, dType);
                offset += bLength;
                break;
            }
        }
    }

    if (interface_count != parsed_interfaces) {
        ESP_LOGE(TAG, "expected interface counts to match. %u != %u", interface_count, parsed_interfaces);
        goto fail;
    }

    // merge alternate interfaces (i.e. interfaces with the same bInterfaceNum)
    uint16_t unique_interfaces_count;
    xesp_usb_interface_t** unique_interfaces = merge_alternate_interfaces(pool,
                                                                  all_interfaces, 
                                                                  interface_count,
                                                                  &unique_interfaces_count);

    // these should match
    if (unique_interfaces_count != config->bNumInterfaces){
        ESP_LOGE(TAG, "expected unique interfaces to match bNumInterfaces. %u != %u", 
            unique_interfaces_count, config->bNumInterfaces);
        goto fail;
    }           

    // finally, fill in the config object to return
    xesp_usb_config_descriptor_t* xConfig = malloc_pool(pool,
                                                        sizeof(xesp_usb_config_descriptor_t),
                                                        true); // zero'd
    xConfig->interface_count = config->bNumInterfaces;
    xConfig->interfaces = unique_interfaces;
    xConfig->_malloc_pool = pool;

    // copy the config desciptor into a buffer the xConfig owns
    memcpy(&xConfig->val, data, sizeof(usb_desc_cfg_t));

    return xConfig;

fail:
    malloc_pool_free_all_ptrs(pool);
    return NULL;
}


void xesp_usbh_parse_free_config(xesp_usb_config_descriptor_t* descriptor) {
    malloc_pool_free_all_ptrs(descriptor->_malloc_pool);
}


//////////////////////////////////
//  Print 
//

// interface desc
void xesp_usbh_print_interface_descriptor(xesp_usb_interface_descriptor_t* xIntfDesc){

    if (xIntfDesc == NULL){
        printf("interface descriptor: NULL\n");
        return;
    }

    usb_util_print_intf(&xIntfDesc->val);
    printf("extras length: %u\n", xIntfDesc->extras_length);

    for (uint16_t i = 0; i < xIntfDesc->endpoint_count; i++){

        xesp_usb_endpoint_descriptor_t* xEp = xIntfDesc->endpoints[i];

        usb_util_print_ep(&xEp->val);
        printf("extras length: %u\n", xEp->extras_length);
    }
}

// interface
void xesp_usbh_print_interface(xesp_usb_interface_t* xInterface){

    if (xInterface == NULL){
        printf("interface: NULL\n");
        return;
    }

    // loop through the alt settings of this interface
    for (int k = 0; k < xInterface->altSettings_count; k++){

        xesp_usb_interface_descriptor_t* xIntfDesc = xInterface->altSettings[k];

        xesp_usbh_print_interface_descriptor(xIntfDesc);
    } 
}

// config desc
void xesp_usbh_print_config_descriptor(xesp_usb_config_descriptor_t* config_desc){

    if (config_desc == NULL){
        printf("config: NULL\n");
        return;
    }

    usb_util_print_cfg(&config_desc->val);

    // loop through interfaces in the config
    for (uint16_t i = 0; i < config_desc->interface_count; i++){

        xesp_usb_interface_t* interface = config_desc->interfaces[i];

        xesp_usbh_print_interface(interface);
    }
}