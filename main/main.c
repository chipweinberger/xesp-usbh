

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "usb_utils.h"

#include "xesp_usbh.h"

static const char* TAG = "main";

const char* midi_node_name(uint8_t byte){
    switch (byte % 12){
        case 0: return "C";
        case 1: return "Db";
        case 2: return "D";
        case 3: return "Eb";
        case 4: return "E";
        case 5: return "F";
        case 6: return "Gb";
        case 7: return "G";
        case 8: return "Ab";
        case 9: return "A";
        case 10: return "Bb";
        case 11: return "B";
        default: return "N/A";
    }
}

uint8_t midi_note_octave(uint8_t byte){
    return byte / 12;
}

void midi_event(uint8_t* event) {
	if (event[0] == 0 &&
		event[1] == 0 &&
		event[2] == 0 &&
		event[3] == 0){
		//ignore
		return;
	}
	uint8_t channel = (event[0] >> 4) & 15;
    uint8_t cin = event[0] & 15; // code index number
	switch(cin) {
	case 8:
		printf("ch %d note off %02x %02x %02x : %s%u\n", channel, 
            event[1], event[2], event[3], midi_node_name(event[2]), midi_note_octave(event[2]));
        break;
	case 9:
		printf("ch %d note on  %02x %02x %02x : %s%u velocity:%u\n", channel,
            event[1], event[2], event[3], 
            midi_node_name(event[2]), midi_note_octave(event[2]), event[3]);
        break;
	case 11:
		printf("ch %d ctrl     %02x %02x %02x\n", channel, 
            event[1], event[2], event[3]);
        break;
	case 14:
		printf("ch %d pitch    %02x %02x %02x\n", channel,
             event[1], event[2], event[3]);
        break;
	default:
		printf("ch %d ?        %02x %02x %02x\n", channel, 
            event[1], event[2], event[3]);
        break;
	}
}

void app_main(void)
{
    printf("Hello world USB host!\n");

    xesp_usbh_init();

    uint64_t open_count = 0;

open_device:

    printf("opening device...\n");

    // block until we open a device
    xesp_usb_device_t device = xesp_usbh_open_device(&open_count);

    usb_desc_devc_t2 descriptor;
    hcd_pipe_event_t rc = xesp_usbh_get_device_descriptor(device, &descriptor);

    if (rc == XUSB_OK) {
        usb_util_print_devc(&descriptor);
    } else {
        ESP_LOGE(TAG, "could not get device descriptor");
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
    // set the midi configuration
    rc = xesp_usbh_set_config(device, 1);
    if(rc != XUSB_OK) {
        ESP_LOGE(TAG, "could not set config");
    }

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
    } else {
        printf("found midi configurration %i\n", midi_config->val.bConfigurationValue);
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

    //ESP_LOGI(TAG, "out pipe: %p", midi_pipe_out);
    ESP_LOGI(TAG, "in pipe: %p", midi_pipe_in);

    uint8_t* data_buff = calloc(1, XESP_USB_MAX_XFER_BYTES);

    while (true){

        // xfer in data
        uint16_t num_bytes_transfered;
        rc = xesp_usbh_xfer_from_pipe(midi_pipe_in, data_buff, &num_bytes_transfered);
        if (rc != XUSB_OK) {
            ESP_LOGE(TAG, "xfer midi IN pipe fail: %s", hcd_pipe_event_str(rc));
            goto open_device;
        } 
        
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data_buff, num_bytes_transfered, ESP_LOG_INFO);


		for(int i = 0; i < num_bytes_transfered; i += 4) {
			// each midi event is 4 bytes
			midi_event(data_buff + i);
		}
	}
}