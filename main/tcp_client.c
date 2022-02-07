/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "addr_from_stdin.h"
#include "lwip/err.h"
#include "lwip/sockets.h"


#if defined(CONFIG_EXAMPLE_IPV4)
#define HOST_IP_ADDR "1.58.62.16"
#elif defined(CONFIG_EXAMPLE_IPV6)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV6_ADDR
#else
#define HOST_IP_ADDR ""
#endif

#define PORT 10240

static const char *TAG = "example";
static const char *payload = "Echo";

/* 宏定义WiFi名称和密码 */
#define MY_WIFI_SSID    "mix2s"
#define MY_WIFI_PASSWD  "zxcvbnm."

/* 宏定义WiFi连接事件标志位和连接失败标志位 */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* 定义一个WiFi连接事件标志组句柄 */
static EventGroupHandle_t wifi_event_group_handler;

//函数声明
static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data);

static void tcp_client_task(void *pvParameters)
{
    char rx_buffer[128];
    char host_ip[] = HOST_IP_ADDR;
    int addr_family = 0;
    int ip_protocol = 0;

    while (1) {
#if defined(CONFIG_EXAMPLE_IPV4)
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
#elif defined(CONFIG_EXAMPLE_IPV6)
        struct sockaddr_in6 dest_addr = { 0 };
        inet6_aton(host_ip, &dest_addr.sin6_addr);
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        dest_addr.sin6_scope_id = esp_netif_get_netif_impl_index(EXAMPLE_INTERFACE);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
        struct sockaddr_storage dest_addr = { 0 };
        ESP_ERROR_CHECK(get_addr_from_stdin(PORT, SOCK_STREAM, &ip_protocol, &addr_family, &dest_addr));
#endif                                                                                                                                                                                                                                                                                                                                                                   
        int sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, PORT);

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0) {                   
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;           
        }
        ESP_LOGI(TAG, "Successfully connected");

        while (1) {
            int err = send(sock, payload, strlen(payload), 0);
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                break;
            }

            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);
                ESP_LOGI(TAG, "%s", rx_buffer);
            }

            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    // ESP_ERROR_CHECK(example_connect());
    // example_connect("mix2s", "zxcvbnm.");




    /* 创建一个事件标志组 */
    wifi_event_group_handler = xEventGroupCreate();

    /* 创建一个默认的WIFI-STA网络接口，如果初始化错误，此API将中止。*/
    esp_netif_create_default_wifi_sta();				

    /* 使用WIFI_INIT_CONFIG_DEFAULT() 来获取一个默认的wifi配置参数结构体变量*/
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* 根据cfg参数初始化wifi连接所需要的资源 */
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	/* 将事件处理程序注册到系统默认事件循环，分别是WiFi事件和IP地址事件 */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* 定义WiFi连接的ssid和password参数 */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = "mix2s" ,
            .password = "zxcvbnm."  
        },
    };
    /* 设置WiFi的工作模式为 STA */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    /* 设置WiFi连接的参数，主要是ssid和password */
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    /* 启动WiFi连接 */
    ESP_ERROR_CHECK(esp_wifi_start());
    
    printf("wifi_init_sta finished. \n");







    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
}

/* 系统事件循环处理函数 */
static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{
    static int retry_num = 0;           /* 记录wifi重连次数 */
    /* 系统事件为WiFi事件 */
    if (event_base == WIFI_EVENT) 
    {
        if(event_id == WIFI_EVENT_STA_START)    /* 事件id为STA开始 */
            esp_wifi_connect();
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED) /* 事件id为失去STA连接 */ 
        {
            if (retry_num < 10)  /* WiFi重连次数小于10 */
            {
                esp_wifi_connect();
                retry_num++;
                printf("retry to connect to the AP %d times. \n",retry_num);
            } 
            else 
            {
                /* 将WiFi连接事件标志组的WiFi连接失败事件位置1 */
                xEventGroupSetBits(wifi_event_group_handler, WIFI_FAIL_BIT);
            }
        }
    } 
    /* 系统事件为ip地址事件，且事件id为成功获取ip地址 */
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data; /* 获取IP地址信息*/
        printf("got ip:%d.%d.%d.%d" , IP2STR(&event->ip_info.ip));  /* 打印ip地址*/
        retry_num = 0;                                              /* WiFi重连次数清零 */
        /* 将WiFi连接事件标志组的WiFi连接成功事件位置1 */
        xEventGroupSetBits(wifi_event_group_handler, WIFI_CONNECTED_BIT);
    }
}
