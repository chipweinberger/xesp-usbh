

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "usb_utils.h"

#include "xesp_usbh.h"

static const char* TAG = "main";

/*


//-----------------------------------------------------------------------------
Simple driver for a MIDI keyboard
Open the device with the provided VID/PID.
Bulk read from the first input endpoint.
Interpret the data as USB MIDI events.
//-----------------------------------------------------------------------------

package main

import (
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"github.com/deadsy/libusb"
)

type ep_info struct {
	itf int
	ep  *libusb.Endpoint_Descriptor
}

var quit bool = false

const NOTES_IN_OCTAVE = 12

func midi_note_name(note byte, mode string) string {
	sharps := [NOTES_IN_OCTAVE]string{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}
	flats := [NOTES_IN_OCTAVE]string{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"}
	note %= NOTES_IN_OCTAVE
	if mode == "#" {
		return sharps[note]
	}
	return flats[note]
}

// return a note name with sharp and flat forms
func midi_full_note_name(note byte) string {
	s_name := midi_note_name(note, "#")
	f_name := midi_note_name(note, "b")
	if s_name != f_name {
		return fmt.Sprintf("%s/%s", s_name, f_name)
	}
	return s_name
}

func midi_event(event []byte) {
	if event[0] == 0 &&
		event[1] == 0 &&
		event[2] == 0 &&
		event[3] == 0 {
		//ignore
		return
	}
	ch := (event[0] >> 4) & 15
	switch event[0] & 15 {
	case 8:
		fmt.Printf("ch %d note off %02x %02x %02x %s\n", ch, event[1], event[2], event[3], midi_full_note_name(event[2]))
	case 9:
		fmt.Printf("ch %d note on  %02x %02x %02x %s\n", ch, event[1], event[2], event[3], midi_full_note_name(event[2]))
	case 11:
		fmt.Printf("ch %d ctrl     %02x %02x %02x\n", ch, event[1], event[2], event[3])
	case 14:
		fmt.Printf("ch %d pitch    %02x %02x %02x\n", ch, event[1], event[2], event[3])
	default:
		fmt.Printf("ch %d ?        %02x %02x %02x\n", ch, event[1], event[2], event[3])
	}
}

func midi_device(ctx libusb.Context, vid uint16, pid uint16) {
	fmt.Printf("Opening device %04X:%04X ", vid, pid)
	hdl := libusb.Open_Device_With_VID_PID(ctx, vid, pid)
	if hdl == nil {
		fmt.Printf("failed (do you have permission?)\n")
		return
	}
	fmt.Printf("ok\n")
	defer libusb.Close(hdl)

	dev := libusb.Get_Device(hdl)
	dd, err := libusb.Get_Device_Descriptor(dev)
	if err != nil {
		fmt.Printf("%s\n", err)
		return
	}

	// record information on input endpoints
	ep_in := make([]ep_info, 0, 1)
	midi_found := false

	for i := 0; i < int(dd.BNumConfigurations); i++ {
		cd, err := libusb.Get_Config_Descriptor(dev, uint8(i))
		if err != nil {
			fmt.Printf("%s\n", err)
			return
		}
		// iterate across endpoints
		for _, itf := range cd.Interface {
			for _, id := range itf.Altsetting {
				if id.BInterfaceClass == libusb.CLASS_AUDIO && id.BInterfaceSubClass == 3 {
					midi_found = true
				}
				for _, ep := range id.Endpoint {
					if ep.BEndpointAddress&libusb.ENDPOINT_IN != 0 {
						ep_in = append(ep_in, ep_info{itf: i, ep: ep})
					}
				}
			}
		}

		libusb.Free_Config_Descriptor(cd)
	}

	if midi_found == false || len(ep_in) == 0 {
		fmt.Printf("no midi inputs found\n")
		return
	}

	fmt.Printf("num input endpoints %d\n", len(ep_in))
	libusb.Set_Auto_Detach_Kernel_Driver(hdl, true)

	// claim the interface
	err = libusb.Claim_Interface(hdl, ep_in[0].itf)
	if err != nil {
		fmt.Printf("%s\n", err)
		return
	}
	defer libusb.Release_Interface(hdl, ep_in[0].itf)

	data := make([]byte, ep_in[0].ep.WMaxPacketSize)
	for quit == false {
		data, err := libusb.Bulk_Transfer(hdl, ep_in[0].ep.BEndpointAddress, data, 1000)
		if err == nil {
			for i := 0; i < len(data); i += 4 {
				// each midi event is 4 bytes
				midi_event(data[i : i+4])
			}
		}
	}
}

func midi_main() int {
	var ctx libusb.Context
	err := libusb.Init(&ctx)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err)
		return -1
	}
	defer libusb.Exit(ctx)

	midi_device(ctx, 0x0944, 0x0115) // Korg Nano Key 2
	//midi_device(ctx, 0x041e, 0x3f0e) // Creative Technology, E-MU XMidi1X1 Tab

	return 0
}

func main() {

	sigs := make(chan os.Signal, 1)
	signal.Notify(sigs, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		sig := <-sigs
		fmt.Printf("\n%s\n", sig)
		quit = true
	}()

	os.Exit(midi_main())
}

*/

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