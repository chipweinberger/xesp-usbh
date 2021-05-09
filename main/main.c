

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "usb_utils.h"

#include "xesp_usbh.h"

static const char* TAG = "main";

void app_main(void)
{
    printf("Hello world USB host!\n");

    xesp_usbh_init();

    // block until we open a device
    uint64_t open_count = 0;
    xesp_usb_device_t device = xesp_usbh_open_device(&open_count);

    usb_desc_devc_t2 descriptor;
    hcd_pipe_event_t rc = xesp_usbh_get_device_descriptor(device, &descriptor);

    if (rc == XUSB_OK) {
        usb_util_print_devc(&descriptor);
    } else {
        ESP_LOGE(TAG, "could not get device descriptor");
    }

    rc = xesp_usbh_set_addr(device, 1);
    if(rc != XUSB_OK) {
        ESP_LOGE(TAG, "could not set addr");
    }

    // get strings
    char * str;
    rc = xesp_usbh_get_string_descriptor(device, descriptor.iManufacturer,&str);
    if(rc == XUSB_OK){
        printf("manufacturer: %s\n", str);
        free(str);
    }

    rc = xesp_usbh_get_string_descriptor(device, descriptor.iProduct,&str);
    if(rc == XUSB_OK){
        printf("product: %s\n", str);
        free(str);
    }

    rc = xesp_usbh_get_string_descriptor(device, descriptor.iSerialNumber,&str);
    if(rc == XUSB_OK){
        printf("serial: %s\n", str);
        free(str);
    }

    //
    // midi
    //

    // record information on input endpoints
    xesp_usb_config_descriptor_t* midi_config = NULL;
	xesp_usb_endpoint_descriptor_t* ep_midi_in = NULL;
    //xesp_usb_endpoint_descriptor_t* ep_midi_out = NULL;
	bool midi_found = false;

    // min 1 config
    uint8_t num_configs = descriptor.bNumConfigurations ? descriptor.bNumConfigurations : 1;

	for(int iConf = 0; iConf < num_configs; iConf++) {

		xesp_usb_config_descriptor_t* config;
        rc = xesp_usbh_get_config_descriptor(device, iConf, &config);
        if (rc != XUSB_OK) {
            ESP_LOGE(TAG, "could not get config descriptor %u", iConf);
            continue;
        }

        ESP_LOGI(TAG, "print config");
        xesp_usbh_print_config_descriptor(config);

        // loop interfaces
        for (int iIntf = 0; iIntf < config->interface_count; iIntf++){

            xesp_usb_interface_t* xIntf = config->interfaces[iIntf];

            // loop alternate interfaces
            for (int iAlt = 0; iAlt < xIntf->altSettings_count; iAlt++){

                xesp_usb_interface_descriptor_t* xIntfDesc = xIntf->altSettings[iAlt];

                if (!midi_found &&
                    xIntfDesc->val.bInterfaceClass == USB_CLASS_AUDIO &&
                    xIntfDesc->val.bInterfaceSubClass == USB_SUBCLASS_Audio_Midi_Streaming) {

                    midi_found = true;

                    midi_config = config;

                    //loop endpoints
                    for(int iEp = 0; iEp < xIntfDesc->endpoint_count; iEp++){

                        xesp_usb_endpoint_descriptor_t* xEp = xIntfDesc->endpoints[iEp];

                        uint8_t dir = USB_DESC_EP_GET_EP_DIR(&xEp->val);

                        if (dir == 1){
                            printf("\n\nfound ep_midi_in\n");
                            ep_midi_in = xEp;
                        } else {
                            //ep_midi_out = xEp;
                        }
                    }
                }
            }
        }

        if (config != midi_config) {
		    xesp_usbh_free_config_descriptor(config);
        }
	}

    if (!midi_found){
        ESP_LOGE(TAG, "no midi device found");
        while (true){
            vTaskDelay(500);
        }
    }

    // set the midi configuration
    rc = xesp_usbh_set_config(device, 1);
    if(rc != XUSB_OK) {
        ESP_LOGE(TAG, "could not set config");
    }

    // set the midi alt interface?

    // open endpoints
    //hcd_pipe_handle_t* midi_pipe_out = xesp_usbh_open_endpoint(device, &ep_midi_out->val);

    // in endpoint - data flows in to the host
    hcd_pipe_handle_t* midi_pipe_in = xesp_usbh_open_endpoint(device, &ep_midi_in->val);

    // free the config
    //xesp_usbh_free_config_descriptor(midi_config);

    uint8_t* data_buff = calloc(1, XESP_USB_MAX_XFER_BYTES);

    while (true){
        vTaskDelay(50);

        //ESP_LOGI(TAG, "out pipe: %p", midi_pipe_out);
        ESP_LOGI(TAG, "in pipe: %p", midi_pipe_in);

/*
        rc = xesp_usbh_xfer_from_pipe(midi_pipe_out, data_buff);
        if (rc != XUSB_OK) {
            ESP_LOGE(TAG, "xfer midi OUT pipe fail: %s", hcd_pipe_event_str(rc));
        }
*/
        rc = xesp_usbh_xfer_from_pipe(midi_pipe_in, data_buff);
        if (rc != XUSB_OK) {
            ESP_LOGE(TAG, "xfer midi IN pipe fail: %s", hcd_pipe_event_str(rc));
            while (true){
                vTaskDelay(500);
            }
        }
    }
}