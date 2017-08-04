# MQTT Secure via HTTPS Websocket Example

Uses an mbedTLS socket to make a very simple SSL/TLS request over a secure connection, including verifying the server TLS certificate. The MQTT Broker being used here is mqtt.iotcebu.com, an MQTT broker open for use with the IOT Cebu community. You can also use this example to connect to your own broker or to readily available cloud mqtt brokers - iot.eclipse.org, test.mosquitto.org or cloudmqtt.com, etc. - you would have to modify cert.c and replace it with the server certificate you are connecting to. 

Note for mqtt.iotcebu.com: 

To request for access, send and email to vpcola@gmail.com, and I will grant access as soon as I can. The MQTT broker does not verify client certificates and uses the usual username/password to logon to mqtt. There are rules governing which topic you are allowed to publish/subscribe so please pay attention to the email that will be sent after you have requested access. A secure MQTT port (8883) and a separate secure websocket (8083) is also available. The good thing about mqtt.iotcebu.com is that all your mqtt messages are backed up on a database (mysql), where you have read/write access to the database upon approval of your request.

## Credits to the Original Source
* The source code was adapted from pcbreflux - https://github.com/pcbreflux/espressif/tree/master/esp32/app/ESP32_mqtts_gpio
* I only modified a few sections of the base code to adapt to my setup.

## Example MQTT
* Send Topic iotcebu/<username> Message <0 - 100>  to switch first GPIO 19 ON (100 == 100% duty cycle)

## esp-idf used
* commit fd3ef4cdfe1ce747ef4757205f4bb797b099c9d9
* Merge: 94a61389 52c378b4
* Author: Angus Gratton <angus@espressif.com>
* Date:   Fri Apr 21 12:27:32 2017 +0800


## eclipse
* include include.xml to C-Paths

## PREPARE
1. change main/cert.c -> server_root_cert
2. change main/mqtt_subscribe_main.c
    * `#define MQTT_SERVER "your server"`
    * `#define MQTT_USER "your user"`
    * `#define MQTT_PASS "your password"`
    * `#define MQTT_PORT your port`
    * `//#define MQTT_WEBSOCKET 0  // 0=no 1=yes`
    * `#define MQTT_WEBSOCKET 1  // 0=no 1=yes`

(Hint: today mbed_ssl without WebSockets is unstable, i.e. reconnecting is needed some times, searching for reasons...)

## INSTALL
* `make menuconfig`
* `make -j8 all`
* `make flash`
* `make monitor`




