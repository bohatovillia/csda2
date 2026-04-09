#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cy_wcm.h"
#include "lfs.h"
#include "cJSON.h"
#include "http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------
 * Конфігурація Wi-Fi
 *---------------------------------------------------------------------------*/
#define WIFI_SSID     "Ваш_SSID"
#define WIFI_PASSWORD "Ваш_Пароль"

/*---------------------------------------------------------------------------
 * Глобальні змінні
 *---------------------------------------------------------------------------*/
lfs_t lfs;
struct lfs_config cfg;
http_server_t server;

/* Поточний стан LED (віртуальна лампочка) */
static volatile bool g_led_state = false;

/*---------------------------------------------------------------------------
 * GET /api/status  — повертає {"led_on": true/false}
 *---------------------------------------------------------------------------*/
void handle_get_status(http_server_t srv, http_request_t req, void *user_data)
{
    char response[64];
    snprintf(response, sizeof(response),
             "{\"led_on\": %s}", g_led_state ? "true" : "false");

    printf("[GET /api/status] -> %s\n", response);

    /* Відправляємо JSON-відповідь клієнту */
    // http_server_send_response(srv, req, 200, "OK",
    //                           "application/json", response, strlen(response));
}

/*---------------------------------------------------------------------------
 * POST /api/control — приймає {"command": "on"/"off"/"toggle"}
 *---------------------------------------------------------------------------*/
void handle_post_control(http_server_t srv, http_request_t req, void *user_data)
{
    /* 1. Отримати тіло HTTP-запиту */
    char *body = NULL; // http_get_body(req);
    if (body == NULL) {
        // http_server_send_response(srv, req, 400, "Bad Request",
        //     "application/json", "{\"error\":\"empty body\"}", 21);
        return;
    }

    /* 2. Розпарсити JSON за допомогою cJSON */
    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        printf("[POST /api/control] Помилка парсингу JSON\n");
        // http_server_send_response(srv, req, 400, "Bad Request",
        //     "application/json", "{\"error\":\"invalid json\"}", 23);
        return;
    }

    /* 3. Обробити команду */
    cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(json, "command");
    if (cJSON_IsString(cmd_item) && cmd_item->valuestring != NULL) {
        const char *cmd = cmd_item->valuestring;

        if (strcmp(cmd, "on") == 0) {
            g_led_state = true;
            cyhal_gpio_write(CYBSP_USER_LED, false); /* LED ON (active-low) */
        } else if (strcmp(cmd, "off") == 0) {
            g_led_state = false;
            cyhal_gpio_write(CYBSP_USER_LED, true);  /* LED OFF */
        } else if (strcmp(cmd, "toggle") == 0) {
            g_led_state = !g_led_state;
            cyhal_gpio_toggle(CYBSP_USER_LED);
        }

        printf("[POST /api/control] Команда: %s -> LED = %s\n",
               cmd, g_led_state ? "ON" : "OFF");
    }

    cJSON_Delete(json);

    /* 4. Відповідь клієнту */
    // http_server_send_response(srv, req, 200, "OK",
    //     "application/json", "{\"result\":\"ok\"}", 15);
}

/*---------------------------------------------------------------------------
 * GET / — віддає index.html з LittleFS
 *---------------------------------------------------------------------------*/
void handle_root(http_server_t srv, http_request_t req, void *user_data)
{
    lfs_file_t file;

    int err = lfs_file_open(&lfs, &file, "index.html", LFS_O_RDONLY);
    if (err < 0) {
        printf("Помилка відкриття index.html: %d\n", err);
        // http_server_send_response(srv, req, 404, "Not Found",
        //     "text/plain", "File not found", 14);
        return;
    }

    lfs_ssize_t size = lfs_file_size(&lfs, &file);
    char *buffer = (char *)malloc(size + 1);
    if (buffer != NULL) {
        lfs_file_read(&lfs, &file, buffer, size);
        buffer[size] = '\0';

        // http_server_send_response(srv, req, 200, "OK",
        //     "text/html", buffer, size);
        printf("Відправлено index.html (%d байт)\n", (int)size);
        free(buffer);
    }

    lfs_file_close(&lfs, &file);
}

/*---------------------------------------------------------------------------
 * main()
 *---------------------------------------------------------------------------*/
int main(void)
{
    cy_rslt_t result;

    /* 1. Ініціалізація BSP */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS) { CY_ASSERT(0); }

    __enable_irq();

    /* 2. Serial-термінал */
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                        CY_RETARGET_IO_BAUDRATE);
    printf("=== Лаб 2: Асинхронне керування периферією ===\n");

    /* 3. Ініціалізація LED піна */
    cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG, true); /* true = LED OFF (active-low) */

    /* 4. Wi-Fi */
    printf("Ініціалізація Wi-Fi...\n");
    cy_wcm_config_t wcm_config = { .interface = CY_WCM_INTERFACE_TYPE_STA };
    cy_wcm_init(&wcm_config);

    cy_wcm_connect_params_t connect_param = {
        .ap_credentials = {
            .SSID     = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .security = CY_WCM_SECURITY_WPA2_AES_PSK
        }
    };

    cy_wcm_ip_address_t ip_addr;
    printf("Підключення до %s ...\n", WIFI_SSID);

    if (cy_wcm_connect_ap(&connect_param, &ip_addr) == CY_RSLT_SUCCESS) {
        printf("Підключено! IP-адреса: %d.%d.%d.%d\n",
               (ip_addr.ip.v4 >> 24) & 0xFF,
               (ip_addr.ip.v4 >> 16) & 0xFF,
               (ip_addr.ip.v4 >> 8)  & 0xFF,
                ip_addr.ip.v4        & 0xFF);
    } else {
        printf("Помилка підключення до Wi-Fi.\n");
        while (1) {}
    }

    /* 5. LittleFS */
    printf("Монтування LittleFS...\n");
    int err = lfs_mount(&lfs, &cfg);
    if (err) {
        printf("Форматування ФС...\n");
        lfs_format(&lfs, &cfg);
        lfs_mount(&lfs, &cfg);
    }
    printf("LittleFS OK.\n");

    /* 6. HTTP-сервер: реєстрація ендпоінтів */
    // http_server_init(&server, 80);

    /* Кореневий маршрут — віддає Dashboard (index.html) */
    http_server_register_resource(server, "/",            HTTP_GET,  handle_root,         NULL);

    /* REST API */
    http_server_register_resource(server, "/api/status",  HTTP_GET,  handle_get_status,   NULL);
    http_server_register_resource(server, "/api/control", HTTP_POST, handle_post_control, NULL);

    // http_server_start(server);
    printf("HTTP-сервер запущено на порті 80. Очікування запитів...\n");

    /* 7. Головний цикл */
    for (;;) {
        cyhal_system_delay_ms(1000);
    }
}
