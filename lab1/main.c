#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cy_wcm.h"
#include "lfs.h"
#include "http_server.h" /* Умовний заголовок HTTP сервера згідно з завданням */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Налаштування локальної мережі Wi-Fi */
#define WIFI_SSID     "Ваш_SSID"
#define WIFI_PASSWORD "Ваш_Пароль"

/* Глобальні змінні для LittleFS та HTTP-сервера */
lfs_t lfs;
struct lfs_config cfg; /* Повинна бути налаштована на функції роботи з Flash (block device) */
http_server_t server;

/* Callback функція (згідно з завданням) */
void resource_handler_cb(http_server_t server, http_request_t request, void *user_data) {
    lfs_file_t file;
    char *buffer = NULL;
    lfs_ssize_t size = 0;
    
    // 1. Відкрити файл "index.html" через lfs_file_open
    int err = lfs_file_open(&lfs, &file, "index.html", LFS_O_RDONLY);
    
    if (err < 0) {
        printf("Помилка відкриття файлу: %d\n", err);
        // Відповідно до API вашого HTTP-сервера відправити 404 (приклад)
        // http_server_send_response(server, request, 404, "Not Found", "text/plain", "File not found", 14);
        return;
    }

    // 2. Прочитати вміст у буфер
    size = lfs_file_size(&lfs, &file);
    buffer = (char *)malloc(size + 1);
    if (buffer != NULL) {
        lfs_file_read(&lfs, &file, buffer, size);
        buffer[size] = '\0'; // Завершити рядок нульовим байтом для безпеки
        
        // 3. Відправити HTTP-відповідь: "200 OK", Content-Type: "text/html"
        // (Тут використовується гіпотетична функція відправки відповіді з вашої бібліотеки HTTP)
        // Приклад:
        // http_server_send_response(server, request, 200, "OK", "text/html", buffer, size);
        printf("Файл 'index.html' успішно зчитано та відправлено. Розмір: %d байт\n", size);
        free(buffer);
    } else {
        printf("Помилка виділення пам'яті для буфера.\n");
    }

    // 4. Закрити файл
    lfs_file_close(&lfs, &file);
}

int main(void) {
    cy_rslt_t result;

    /* 1. Ініціалізація периферії BSP */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(0);
    }

    /* Увімкнути глобальні переривання */
    __enable_irq();

    /* 2. Ініціалізація Serial-терміналу (Retarget-IO) */
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);
    printf("=== Запуск вбудованого вебсервера (Лаб 1) ===\n");

    /* 3. Налаштування Wi-Fi модуля */
    printf("Ініціалізація Wi-Fi...\n");
    cy_wcm_config_t wcm_config = { .interface = CY_WCM_INTERFACE_TYPE_STA };
    cy_wcm_init(&wcm_config);
    
    cy_wcm_connect_params_t connect_param = {
        .ap_credentials = {
            .SSID = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .security = CY_WCM_SECURITY_WPA2_AES_PSK
        }
    };
    
    cy_wcm_ip_address_t ip_addr;
    printf("Підключення до мережі %s...\n", WIFI_SSID);
    
    if (cy_wcm_connect_ap(&connect_param, &ip_addr) == CY_RSLT_SUCCESS) {
        printf("Підключено! IP-адреса: %d.%d.%d.%d\n", 
               ip_addr.ip.v4 >> 24, 
               (ip_addr.ip.v4 >> 16) & 0xFF, 
               (ip_addr.ip.v4 >> 8) & 0xFF, 
               ip_addr.ip.v4 & 0xFF);
    } else {
        printf("Помилка підключення до Wi-Fi.\n");
        while(1) { /* Блокування в разі помилки */ }
    }

    /* 4. Ініціалізація ФС: LittleFS */
    printf("Ініціалізація LittleFS...\n");
    // Примітка: cfg має містити прив'язку до Flash API мікроконтролера,
    // наприклад, функції cy_serial_flash...
    int err = lfs_mount(&lfs, &cfg);
    if (err) {
        printf("Монтування не вдалося, форматування ФС...\n");
        lfs_format(&lfs, &cfg);
        lfs_mount(&lfs, &cfg);
    }
    printf("Файлова система LittleFS змонтована.\n");

    /* 5. Реалізація HTTP-сервера */
    /* (Знову ж таки, використовується загальний API http-server) */
    
    // Ініціалізація (гіпотетично)
    // http_server_init(&server, 80);
    
    // Реєстрація ресурсу
    http_server_register_resource(server, "/", HTTP_GET, resource_handler_cb, NULL);
    
    // Запуск сервера (гіпотетично)
    // http_server_start(server);
    
    printf("Очікування HTTP-запитів на порті 80...\n");

    /* Головний цикл */
    for (;;) {
        // У FreeRTOS тут буде cyhal_system_delay_ms() або vTaskDelay()
        cyhal_system_delay_ms(1000); 
    }
}
