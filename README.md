# AtomVM GPS

This AtomVM library can be used to take readings from common GPS sensors connected to ESP32 devices.

The library assumes that GPS sensors are connected to the ESP32 via the UART interface, and that the GPS sensor supports the [NMEA 0183](https://en.wikipedia.org/wiki/NMEA_0183) protocol.  Examples include boards built on the [NEO-6](https://datasheetspdf.com/pdf-file/866235/u-blox/NEO-6M/1) chipset.  The GPS sensor only needs to be connected to the RX channel on the UART interface, as messages are only sent from the GPS sensor to the ESP32.

Data in a reading includes:

* date and time
* latitude (degrees)
* longitude (degrees)
* altitude (meters)
* speed (meters/sec)
* number of sattelites in use

> Note.  Portions of this library are copyright Espressif Systems (Shanghai) PTE LTD

Documentation for this library can be found in the following sections:

* [Programmer's Guide](markdown/guide.md)
* [Build Instructions](markdown/build.md)
* [Example Program](examples/gps_example/README.md)
