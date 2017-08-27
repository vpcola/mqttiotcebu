/* MQTT Example using plain mbedTLS sockets
 *
 * Adapted from the ssl_client1 example in mbedtls.
 *
 * Original Copyright (C) 2006-2016, ARM Limited, All Rights Reserved, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2017 pcbreflux, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2017 iotcebu, Apache 2.0 License.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>
#include <stdlib.h>

#include "sdkconfig.h"

#include "MQTTClient.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"

#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

#include "DHT22.h"

/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
 */

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

/* Constants that aren't configurable in menuconfig */
#define MQTT_SERVER "mqtt.iotcebu.com"
#define MQTT_USER "vergil"
#define MQTT_PASS "ab12cd34"
#define MQTT_PORT 8083
#define MQTT_TOPIC "iotcebu/vergil/pwm/#"
#define MQTT_CLIENTID "esp32"
#define MQTT_WEBSOCKET 1  // 0=no 1=yes
#define MQTT_BUF_SIZE 512

#define I2C_MASTER_SCL_IO    22    /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO    21    /*!< gpio number for I2C master data  */
#define I2C_MASTER_NUM I2C_NUM_1   /*!< I2C port number for master dev */
#define I2C_MASTER_TX_BUF_DISABLE   0   /*!< I2C master do not need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0   /*!< I2C master do not need buffer */
#define I2C_MASTER_FREQ_HZ    100000     /*!< I2C master clock frequency */
#define ACK_CHECK_EN   0x1     /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS  0x0     /*!< I2C master will not check ack from slave */
#define ACK_VAL    0x0         /*!< I2C ack value */
#define NACK_VAL   0x1         /*!< I2C nack value */

#define MCP23017_ADDR   0x20

#define DHT22_IO   4
#define mainQUEUE_LENGTH 10

static xSemaphoreHandle print_mux;
static QueueHandle_t xQueue = NULL;


static uint8_t led_pin[] = { 16, 17 };
static int led_cnt = sizeof(led_pin)/sizeof(led_pin[0]);

static unsigned char mqtt_sendBuf[MQTT_BUF_SIZE];
static unsigned char mqtt_readBuf[MQTT_BUF_SIZE];

static const char *TAG = "MQTTS";

/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t wifi_event_group;

/**
* @brief i2c master initialization
*/
static void i2c_master_init()
{
   int i2c_master_port = I2C_MASTER_NUM;
   i2c_config_t conf;
   conf.mode = I2C_MODE_MASTER;
   conf.sda_io_num = I2C_MASTER_SDA_IO;
   conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
   conf.scl_io_num = I2C_MASTER_SCL_IO;
   conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
   conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
   i2c_param_config(i2c_master_port, &conf);
   i2c_driver_install(i2c_master_port, conf.mode,
                      I2C_MASTER_RX_BUF_DISABLE,
                      I2C_MASTER_TX_BUF_DISABLE, 0);
}


/**
* @brief test code to read esp-i2c-slave
*        We need to fill the buffer of esp slave device, then master can read them out.
*
* _______________________________________________________________________________________
* | start | slave_addr + rd_bit +ack | read n-1 bytes + ack | read 1 byte + nack | stop |
* --------|--------------------------|----------------------|--------------------|------|
*
*/
static esp_err_t i2c_master_read_slave(i2c_port_t i2c_num, uint8_t slaveaddr, uint8_t* data_rd, size_t size)
{
   if (size == 0) {
       return ESP_OK;
   }
   i2c_cmd_handle_t cmd = i2c_cmd_link_create();
   i2c_master_start(cmd);
   i2c_master_write_byte(cmd, ( slaveaddr << 1 ) | I2C_MASTER_READ, ACK_CHECK_EN);
   if (size > 1) {
       i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
   }
   i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
   i2c_master_stop(cmd);
   esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
   i2c_cmd_link_delete(cmd);
   return ret;
}

/**
* @brief Test code to write esp-i2c-slave
*        Master device write data to slave(both esp32),
*        the data will be stored in slave buffer.
*        We can read them out from slave buffer.
*
* ___________________________________________________________________
* | start | slave_addr + wr_bit + ack | write n bytes + ack  | stop |
* --------|---------------------------|----------------------|------|
*
*/
static esp_err_t i2c_master_write_slave(i2c_port_t i2c_num, uint8_t slaveaddr, uint8_t* data_wr, size_t size)
{
   i2c_cmd_handle_t cmd = i2c_cmd_link_create();
   i2c_master_start(cmd);
   i2c_master_write_byte(cmd, ( slaveaddr << 1 ) | I2C_MASTER_WRITE, ACK_CHECK_EN);
   i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
   i2c_master_stop(cmd);
   esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
   i2c_cmd_link_delete(cmd);
   return ret;
}

/**
* @brief Initialize the MCP23017 port expander
*        First set the register 0x00 (IODIRA) for output (0x00),
*        then set the register 0x01 (IODIRB) for input (0xFF)
**/
static void mcp23017_setup(i2c_port_t i2cnum)
{
    uint8_t data[2];

    // Set IODIRA as output
    data[0] = 0x00; // IODIRA
    data[1] = 0x00; // All outputs

    i2c_master_write_slave(i2cnum, MCP23017_ADDR, &data[0], 2);

    // Set IODIRB as input
    data[0] = 0x01; // IODIRB
    data[1] = 0xFF; // All inputs

    i2c_master_write_slave(i2cnum, MCP23017_ADDR, &data[0], 2);
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            /* This is a workaround as ESP32 WiFi libs don't currently
               auto-reassociate. */
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}
//            .ssid = CONFIG_WIFI_SSID,
//            .password = CONFIG_WIFI_PASSWORD,

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

uint8_t str2unit8(uint8_t *out, const char *s) {
    char *end;
    if (s[0] == '\0')
        return 1;
    errno = 0;
    long l = strtol(s, &end, 10);
    if (l > 255)
        return 1;
    if (l < 0)
        return 1;
    if (*end != '\0')
        return 1;
    *out = (uint8_t)l;
    return 0;
}

void setLED(int gpio_num,uint8_t ledchan,uint8_t duty);  // Forward declaration

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void mqtt_message_handler(MessageData *md) {
    uint8_t duty,gpio,ret;
    char gpoinum[2];
    char dutynum[255];

    ESP_LOGI(TAG, "Topic received!: %.*s %.*s", md->topicName->lenstring.len, md->topicName->lenstring.data, md->message->payloadlen, (char*)md->message->payload);
    gpoinum[0]=*(md->topicName->lenstring.data+md->topicName->lenstring.len-1);
    gpoinum[1]='\0';
    
    sprintf(dutynum,"%.*s",md->message->payloadlen, (char*)md->message->payload);
    ret=str2unit8(&gpio,(const char *)gpoinum);
    if (ret!=0) {
       gpio=0;
    }
    if (gpio>=led_cnt) {
       gpio=led_cnt-1;
    }
    str2unit8(&duty,(const char *)dutynum);
    if (ret!=0) {
       duty=0;
    }

    ESP_LOGI(TAG, "setLED!: %d %d %d %s %d", led_pin[gpio], gpio, duty,gpoinum,md->topicName->lenstring.len);
    setLED(led_pin[gpio],gpio,duty);
}

#pragma GCC diagnostic pop

void setLED(int gpio_num,uint8_t ledchan,uint8_t duty) {

    if (duty>100) {
        duty=100;
    } 

    ledc_timer_config_t timer_conf;
    timer_conf.speed_mode = LEDC_HIGH_SPEED_MODE;
    timer_conf.bit_num    = LEDC_TIMER_10_BIT;
    timer_conf.timer_num  = LEDC_TIMER_0;
    timer_conf.freq_hz    = 1000;
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ledc_conf;
    ledc_conf.gpio_num   = gpio_num;
    ledc_conf.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_conf.channel    = ledchan;
    ledc_conf.intr_type  = LEDC_INTR_DISABLE;
    ledc_conf.timer_sel  = LEDC_TIMER_0;
    ledc_conf.duty       = (0x03FF*(uint32_t)duty)/100;
    ledc_channel_config(&ledc_conf);
}



static void mqtt_task(void *pvParameters)
{
    int ret;
    float temperature = 0.0;
    float humidity = 0.0;

    Network network;

    while(1) {
        ESP_LOGD(TAG,"Wait for WiFi ...");
        /* Wait for the callback to set the CONNECTED_BIT in the
           event group.
         */
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                false, true, portMAX_DELAY);
        ESP_LOGD(TAG, "Connected to AP");

        ESP_LOGD(TAG, "Start MQTT Task ...");

        MQTTClient client;
        NetworkInit(&network);
        network.websocket = MQTT_WEBSOCKET;

        ESP_LOGD(TAG,"NetworkConnect %s:%d ...",MQTT_SERVER,MQTT_PORT);
        ret = NetworkConnect(&network, MQTT_SERVER, MQTT_PORT);
        if (ret != 0) {
            ESP_LOGI(TAG, "NetworkConnect not SUCCESS: %d", ret);
            goto exit;
        }
        ESP_LOGD(TAG,"MQTTClientInit  ...");
        MQTTClientInit(&client, &network,
                2000,            // command_timeout_ms
                mqtt_sendBuf,         //sendbuf,
                MQTT_BUF_SIZE, //sendbuf_size,
                mqtt_readBuf,         //readbuf,
                MQTT_BUF_SIZE  //readbuf_size
                );

        char buf[30];
        MQTTString clientId = MQTTString_initializer;
        sprintf(buf, MQTT_CLIENTID);
        ESP_LOGI(TAG,"MQTTClientInit  %s",buf);
        clientId.cstring = buf;

        MQTTString username = MQTTString_initializer;
        username.cstring = MQTT_USER;

        MQTTString password = MQTTString_initializer;
        password.cstring = MQTT_PASS;

        MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
        data.clientID          = clientId;
        data.willFlag          = 0;
        data.MQTTVersion       = 4; // 3 = 3.1 4 = 3.1.1
        data.keepAliveInterval = 5;
        data.cleansession      = 1;
        data.username          = username;
        data.password          = password;

        ESP_LOGI(TAG,"MQTTConnect  ...");
        ret = MQTTConnect(&client, &data);
        if (ret != SUCCESS) {
            ESP_LOGI(TAG, "MQTTConnect not SUCCESS: %d", ret);
            goto exit;
        }

        ESP_LOGI(TAG, "MQTTSubscribe  ...");
        ret = MQTTSubscribe(&client, MQTT_TOPIC, QOS0, mqtt_message_handler);
        if (ret != SUCCESS) {
            ESP_LOGI(TAG, "MQTTSubscribe: %d", ret);
            goto exit;
        }

#if defined(MQTT_TASK)
        if ((ret = MQTTStartTask(&client)) != pdPASS)
        {
                    ESP_LOGE(TAG,"Return code from start tasks is %d\n", ret);
        }
        ESP_LOGI(TAG, "MQTT Task started!\n");
#endif

            // ESP_LOGI(TAG, "MQTTYield  ...");
        while(1) {
#if !defined(MQTT_TASK)
            ret = MQTTYield(&client, (data.keepAliveInterval+1)*1000);
            if (ret != SUCCESS) {
                ESP_LOGI(TAG, "MQTTYield: %d", ret);
                goto exit;
            }
#endif
            char msgbuf[200];
            // Read the sensor and publish data to MQTT 
            int i = 0;
            do{
                ret = readDHT();
                if (ret != DHT_OK)
                    errorHandler(ret);
                else
                {
                    temperature = getTemperature();
                    humidity = getHumidity();

                    ESP_LOGI(TAG, "Temperature : %.1f", temperature);
                    ESP_LOGI(TAG, "Humidity : %.1f", humidity);
                    break;
                }

                vTaskDelay(300 / portTICK_PERIOD_MS);
                i++;
            }while((ret != DHT_OK) && (i < 5)); // Number of retries = 5

            if (ret == DHT_OK)
            {
                // Publish if we are able to get data from the sensor
                MQTTMessage message;
                sprintf(msgbuf, "{\"temperature\":%.1f,\"humidity\":%.1f}", temperature, humidity);

                ESP_LOGI(TAG, "MQTTPublish  ... %s",msgbuf);
                message.qos = QOS0;
                message.retained = false;
                message.dup = false;
                message.payload = (void*)msgbuf;
                message.payloadlen = strlen(msgbuf)+1;

                ret = MQTTPublish(&client, "iotcebu/vergil/weather", &message);
                if (ret != SUCCESS) {
                    ESP_LOGI(TAG, "MQTTPublish not SUCCESS: %d", ret);
                    goto exit;
                }
            }

                    // Wait until signalled and publish data
            vTaskDelay( 3000 / portTICK_RATE_MS );

        }
exit:
        MQTTDisconnect(&client);
        NetworkDisconnect(&network);
        ESP_LOGI(TAG, "Starting again!");
    }
    esp_task_wdt_delete();
    vTaskDelete(NULL);
}


/**
* @brief The led counter thread. This just to show
*        how we can use the mcp23017 to expand our
*        GPIOs. This task shows nothing more than
*        incrementing the value of the output port A
*        of the port expander.
**/
static void i2c_led_task(void* arg)
{
    uint8_t i = 0;
    uint8_t data[2];
    //float temp = 0.0;

    data[0] = 0x12; // Output IOPORTA
    data[1] = i;
    while(1)
    {
        i2c_master_write_slave(I2C_MASTER_NUM, MCP23017_ADDR, &data[0], 2);
        i++;
        data[1] = i;
        
        vTaskDelay( 200 / portTICK_RATE_MS);
    }
}

void app_main()
{

    nvs_flash_init();
    i2c_master_init();
    initialise_wifi();

    mcp23017_setup(I2C_MASTER_NUM);

    // Create the queue
    xQueue = xQueueCreate( mainQUEUE_LENGTH, sizeof( unsigned long ) );

    xTaskCreate(&mqtt_task, "mqtt_task", 12288, NULL, 5, NULL);
    xTaskCreate(i2c_led_task, "i2c_led_task", 1024 * 2, (void* ) 0, 10, NULL);

}
