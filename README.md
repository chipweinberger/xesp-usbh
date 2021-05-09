## ESP32 S2 libusb inspired library for USB Host

See main.c for example usage.

The control endpoint is all working, but bulk transfer does not seem to work. After Enqueing I get a pipe halted error, which seems to stem from the hardware signalling something has gone wrong.
