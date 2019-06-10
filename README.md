# electrophys_trigger

Wireless trigger device to synchronize remove devices with electrophysiological equipment

## Hardware

This code is designed to run on an ESP32 development board. The Adafruit [HUZZAH32](https://www.adafruit.com/product/3405) is a good choice, since it has a built-in lipo battery charger with a pre-wired battery level monitoring circuit.

### Connecting to the electrophys device

The server will encode the timestamps it receives from the client into a single byte with "byte_code = timestamp %  255 + 1". The eight bits are then sent to the electrophys recoding device either via eight digital output pins or by sending an analog voltage on one of the two ADC pins (see the code for details on this). The default pulse duration is about 10 ms, but this is one of the configurable parameters.

## Dependencies

You'll need Arduino installed and [configured for ESP32 delvelopment](https://github.com/espressif/arduino-esp32#installation-instructions). In addition, there's a modified [NTPClient librabry](https://travis-ci.org/arduino-libraries/NTPClient) that adds millisecond-support for unix epochs. (Should probably do a proper fork of this and make it a sub-module.)

## USAGE

The server will start with a default configuration file which means that it probably can't connect to your WiFi. In that case, it should switch to AP mode with and open SSID at "ephys_trigger" and run a simple configration server accessible at http://192.168.4.1 that you can use to set up the network credentials and other configuration options.

### API

    * curl -X GET http://192.168.4.1/config
    * curl -X POST -H "Content-Type: application/json" --data '{"ssid":"SSID","passwd":"PASSWORD"}' http://192.168.4.1/config
    * curl -X GET http://192.168.4.1/timestamp
    * curl -X POST -H "Content-Type: application/json" --data '{"timestamp":681480000}' http://192.168.4.1/timestamp
    * curl -X GET http://192.168.4.1/log
    * curl -X GET http://192.168.4.1/log
    * curl -X POST -H "Content-Type: application/json" --data '{"681480000\tdata1\tdata2\n"}' http://192.168.4.1/event



