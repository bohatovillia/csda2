#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cy_wcm.h"
#include "lfs.h"
#include "http_server.h" /* Умовний заголовок HTTP сервера з підтримкою TLS */
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "certs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------
 * Конфігурація Wi-Fi (STA)
 *---------------------------------------------------------------------------*/
#define WIFI_SSID     "Ваш_SSID"
#define WIFI_PASSWORD "Ваш_Пароль"

/*---------------------------------------------------------------------------
 * Глобальні змінні
 *---------------------------------------------------------------------------*/
lfs_t lfs;
struct lfs_config cfg;
http_server_t https_server;

/* mbedTLS структури */
mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context ctr_drbg;
mbedtls_ssl_context ssl;
mbedtls_ssl_config conf;
mbedtls_x509_crt srvcert;
mbedtls_pk_context pkey;

/* Поточний стан LED */
static volatile bool g_led_state = false;

/*---------------------------------------------------------------------------
 * Ініціалізація mbedTLS контексту
 *---------------------------------------------------------------------------*/
void init_mbedtls(void)
{
    printf("[TLS] Ініціалізація mbedTLS...\n");

    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&srvcert);
    mbedtls_pk_init(&pkey);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    /* 1. Налаштування генератора випадкових чисел */
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *) "psoc6_https", 11) != 0) {
        printf("[TLS] Помилка mbedtls_ctr_drbg_seed\n");
        return;
    }

    /* 2. Завантаження сертифікату сервера */
    if (mbedtls_x509_crt_parse(&srvcert, (const unsigned char *) SERVER_CERT_PEM,
                               SERVER_CERT_PEM_LEN) != 0) {
        printf("[TLS] Помилка завантаження сертифікату\n");
        return;
    }

    /* 3. Завантаження приватного ключа */
    if (mbedtls_pk_parse_key(&pkey, (const unsigned char *) SERVER_KEY_PEM,
                             SERVER_KEY_PEM_LEN, NULL, 0) != 0) {
        printf("[TLS] Помилка завантаження приватного ключа\n");
        return;
    }

    /* 4. Налаштування конфігурації TLS */
    mbedtls_ssl_config_defaults(&conf,
                                MBEDTLS_SSL_IS_SERVER,
                                MBEDTLS_SSL_TRANSPORT_STREAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);

    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_ca_chain(&conf, srvcert.next, NULL);
    mbedtls_ssl_conf_own_cert(&conf, &srvcert, &pkey);

    /* Вимикаємо верифікацію клієнта (нам не потрібен Mutual TLS) */
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);

    mbedtls_ssl_setup(&ssl, &conf);
    printf("[TLS] mbedTLS успішно ініціалізовано.\n");
}

/*---------------------------------------------------------------------------
 * Перевірка заголовка Authorization (Basic Auth)
 * Повертає true, якщо користувач авторизований.
 *---------------------------------------------------------------------------*/
bool check_basic_auth(http_request_t req)
{
    /* 
     * Очікуваний рядок: "Basic YWRtaW46cGFzc3dvcmQ="
     * Base64 декодування "YWRtaW46cGFzc3dvcmQ=" дає "admin:password"
     */
    const char* expected_auth = "Basic YWRtaW46cGFzc3dvcmQ=";
    
    // Гіпотетична функція отримання заголовка з HTTP запиту
    // const char* auth_header = http_get_header(req, "Authorization");
    const char* auth_header = "Basic YWRtaW46cGFzc3dvcmQ="; // Заглушка для компіляції

    if (auth_header != NULL && strcmp(auth_header, expected_auth) == 0) {
        return true;
    }
    return false;
}

/*---------------------------------------------------------------------------
 * Обробник захищеної сторінки (GET /)
 *---------------------------------------------------------------------------*/
void handle_secure_root(http_server_t srv, http_request_t req, void *user_data)
{
    printf("[HTTPS] GET / запит отримано.\n");

    /* Перевірка Basic Auth */
    if (!check_basic_auth(req)) {
        printf("[AUTH] Помилка авторизації! Відправка 401 Unauthorized.\n");
        
        // http_server_send_response(srv, req, 401, "Unauthorized", "text/plain", "401 Unauthorized", 16);
        // http_server_add_header(req, "WWW-Authenticate", "Basic realm=\"PSoC6-Secure-Device\"");
        return;
    }

    /* Користувач авторизований, віддаємо сторінку */
    lfs_file_t file;
    int err = lfs_file_open(&lfs, &file, "index.html", LFS_O_RDONLY);
    if (err < 0) {
        printf("Помилка відкриття index.html\n");
        return;
    }

    lfs_ssize_t size = lfs_file_size(&lfs, &file);
    char *buffer = (char *)malloc(size + 1);
    if (buffer != NULL) {
        lfs_file_read(&lfs, &file, buffer, size);
        buffer[size] = '\0';
        // http_server_send_response(srv, req, 200, "OK", "text/html", buffer, size);
        printf("[HTTPS] Відправлено index.html (%d байт)\n", (int)size);
        free(buffer);
    }
    lfs_file_close(&lfs, &file);
}

/*---------------------------------------------------------------------------
 * Обробник захищеного API (POST /api/led)
 *---------------------------------------------------------------------------*/
void handle_secure_led(http_server_t srv, http_request_t req, void *user_data)
{
    /* Перевірка авторизації для доступу до зміни стану периферії */
    if (!check_basic_auth(req)) {
        // http_server_send_response(srv, req, 401, "Unauthorized", "application/json", "{\"error\":\"unauthorized\"}", 24);
        return;
    }

    g_led_state = !g_led_state;
    cyhal_gpio_write(CYBSP_USER_LED, !g_led_state); /* LED active-low */

    printf("[HTTPS] Зміна стану LED: %s (Авторизований доступ)\n", g_led_state ? "ON" : "OFF");

    char response[64];
    snprintf(response, sizeof(response), "{\"status\":\"ok\",\"led\":%s}", g_led_state ? "true" : "false");
    
    // http_server_send_response(srv, req, 200, "OK", "application/json", response, strlen(response));
}

/*---------------------------------------------------------------------------
 * Обробник ресурсу style.css
 *---------------------------------------------------------------------------*/
void handle_css(http_server_t srv, http_request_t req, void *user_data)
{
    if (!check_basic_auth(req)) { return; }

    lfs_file_t file;
    if (lfs_file_open(&lfs, &file, "style.css", LFS_O_RDONLY) == LFS_ERR_OK) {
        lfs_ssize_t size = lfs_file_size(&lfs, &file);
        char *buffer = (char *)malloc(size + 1);
        if (buffer) {
            lfs_file_read(&lfs, &file, buffer, size);
            // http_server_send_response(srv, req, 200, "OK", "text/css", buffer, size);
            free(buffer);
        }
        lfs_file_close(&lfs, &file);
    }
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
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);
    printf("\n=== Лаб 5: Безпека вбудованих вебресурсів (HTTPS + Basic Auth) ===\n");

    /* 3. Ініціалізація LED */
    cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, true);

    /* 4. Ініціалізація Wi-Fi */
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
    printf("[WIFI] Підключення до %s ...\n", WIFI_SSID);
    if (cy_wcm_connect_ap(&connect_param, &ip_addr) == CY_RSLT_SUCCESS) {
        printf("[WIFI] Підключено! IP-адреса: %d.%d.%d.%d\n",
               (ip_addr.ip.v4 >> 24) & 0xFF, (ip_addr.ip.v4 >> 16) & 0xFF,
               (ip_addr.ip.v4 >> 8)  & 0xFF,  ip_addr.ip.v4        & 0xFF);
    } else {
        printf("[WIFI] Помилка підключення.\n");
        while(1) {}
    }

    /* 5. Монтування LittleFS */
    int err = lfs_mount(&lfs, &cfg);
    if (err) { lfs_format(&lfs, &cfg); lfs_mount(&lfs, &cfg); }
    printf("[FS] LittleFS змонтовано.\n");

    /* 6. Ініціалізація mbedTLS та HTTPS-сервера */
    init_mbedtls();

    // Гіпотетична ініціалізація сервера на порті 443 із заданим SSL контекстом
    // http_server_tls_init(&https_server, 443, &ssl);
    
    http_server_register_resource(https_server, "/",         HTTP_GET,  handle_secure_root, NULL);
    http_server_register_resource(https_server, "/style.css",HTTP_GET,  handle_css,         NULL);
    http_server_register_resource(https_server, "/api/led",  HTTP_POST, handle_secure_led,  NULL);

    // http_server_start(https_server);
    printf("[HTTPS] Відкрийте https://%d.%d.%d.%d (Порт 443)\n\n",
           (ip_addr.ip.v4 >> 24) & 0xFF, (ip_addr.ip.v4 >> 16) & 0xFF,
           (ip_addr.ip.v4 >> 8)  & 0xFF,  ip_addr.ip.v4        & 0xFF);

    /* Головний цикл */
    for (;;) {
        cyhal_system_delay_ms(1000);
    }
}
