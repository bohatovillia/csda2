#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cy_wcm.h"
#include "cy_lwip.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "http_server.h"
#include "cy_em_eeprom.h"        /* Emulated EEPROM for Flash storage */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------
 * Конфігурація SoftAP
 *---------------------------------------------------------------------------*/
#define AP_SSID          "PSoC6_Setup"
#define AP_PASSWORD      "12345678"
#define AP_CHANNEL       6
#define AP_IP            "192.168.1.1"
#define AP_NETMASK       "255.255.255.0"
#define AP_GATEWAY       "192.168.1.1"

/* DNS / HTTP порти */
#define DNS_PORT         53
#define HTTP_PORT        80

/* Emulated EEPROM — область збереження */
#define EEPROM_SIZE           256
#define EEPROM_SSID_OFFSET    0
#define EEPROM_SSID_LEN       64
#define EEPROM_PASS_OFFSET    64
#define EEPROM_PASS_LEN       64
#define EEPROM_MAGIC_OFFSET   128
#define EEPROM_MAGIC_VALUE    0xCAFEBEEF

/* Максимальна кількість знайдених мереж при скануванні */
#define MAX_SCAN_RESULTS      20

/*---------------------------------------------------------------------------
 * Глобальні змінні
 *---------------------------------------------------------------------------*/
http_server_t http_srv;

/* Буфер збережених облікових даних Wi-Fi */
static char saved_ssid[EEPROM_SSID_LEN];
static char saved_password[EEPROM_PASS_LEN];

/* Результати сканування Wi-Fi */
static cy_wcm_scan_result_t scan_results[MAX_SCAN_RESULTS];
static int                   scan_count = 0;
static volatile bool         scan_complete = false;

/* Emulated EEPROM контекст */
static cy_stc_eeprom_context_t eeprom_ctx;
static const cy_stc_eeprom_config_t eeprom_cfg = {
    .eepromSize   = EEPROM_SIZE,
    .simpleMode   = 1,
    .blockingWrite = 1
};

/*---------------------------------------------------------------------------
 * EEPROM: зчитування збережених SSID та Password
 * Повертає true, якщо дані валідні (magic marker присутній)
 *---------------------------------------------------------------------------*/
bool eeprom_read_credentials(char *ssid, char *password)
{
    uint32_t magic = 0;
    cy_em_eeprom_read(EEPROM_MAGIC_OFFSET, &magic, sizeof(magic), &eeprom_ctx);

    if (magic != EEPROM_MAGIC_VALUE) {
        printf("[EEPROM] Дані не знайдено (magic=0x%08lX)\n", (unsigned long)magic);
        return false;
    }

    cy_em_eeprom_read(EEPROM_SSID_OFFSET, ssid, EEPROM_SSID_LEN, &eeprom_ctx);
    cy_em_eeprom_read(EEPROM_PASS_OFFSET, password, EEPROM_PASS_LEN, &eeprom_ctx);

    printf("[EEPROM] Знайдено SSID: \"%s\"\n", ssid);
    return true;
}

/*---------------------------------------------------------------------------
 * EEPROM: збереження SSID та Password у Flash
 *---------------------------------------------------------------------------*/
void eeprom_save_credentials(const char *ssid, const char *password)
{
    uint32_t magic = EEPROM_MAGIC_VALUE;

    char ssid_buf[EEPROM_SSID_LEN]  = {0};
    char pass_buf[EEPROM_PASS_LEN]  = {0};
    strncpy(ssid_buf, ssid, EEPROM_SSID_LEN - 1);
    strncpy(pass_buf, password, EEPROM_PASS_LEN - 1);

    cy_em_eeprom_write(EEPROM_SSID_OFFSET,  ssid_buf, EEPROM_SSID_LEN,  &eeprom_ctx);
    cy_em_eeprom_write(EEPROM_PASS_OFFSET,  pass_buf, EEPROM_PASS_LEN,  &eeprom_ctx);
    cy_em_eeprom_write(EEPROM_MAGIC_OFFSET, &magic,   sizeof(magic),    &eeprom_ctx);

    printf("[EEPROM] Збережено — SSID: \"%s\"\n", ssid);
}

/*---------------------------------------------------------------------------
 * DNS-сервер (UDP порт 53): DNS Spoofing
 *
 * Будь-який DNS-запит отримує відповідь із IP = 192.168.1.1.
 * Це змушує ОС клієнта відкрити Captive Portal.
 *---------------------------------------------------------------------------*/
void dns_server_task(void *arg)
{
    int sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        printf("[DNS] Помилка створення сокета\n");
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(DNS_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("[DNS] Помилка bind на порт %d\n", DNS_PORT);
        lwip_close(sock);
        return;
    }

    printf("[DNS] Сервер запущено на порту %d\n", DNS_PORT);

    uint8_t recv_buf[512];
    uint8_t resp_buf[512];

    /* IP-адреса AP для DNS-відповідей (192.168.1.1) */
    uint8_t ap_ip[4] = {192, 168, 1, 1};

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int recv_len = lwip_recvfrom(sock, recv_buf, sizeof(recv_buf), 0,
                                     (struct sockaddr *)&client_addr, &addr_len);
        if (recv_len < 12) continue;  /* Мінімальний розмір DNS-заголовка */

        /*
         * Формування DNS-відповіді (RFC 1035):
         * 1. Копіювати запит у відповідь
         * 2. Встановити прапорці: QR=1 (відповідь), AA=1 (авторитетна)
         * 3. ANCOUNT = 1
         * 4. Додати Answer RR з IP = 192.168.1.1
         */
        memcpy(resp_buf, recv_buf, recv_len);

        /* Прапорці: QR=1, AA=1, RCODE=0 (No Error) */
        resp_buf[2] = 0x84;   /* QR=1, Opcode=0, AA=1, TC=0, RD=0 */
        resp_buf[3] = 0x00;   /* RA=0, Z=0, RCODE=0               */

        /* ANCOUNT = 1 */
        resp_buf[6] = 0x00;
        resp_buf[7] = 0x01;

        /* Answer Section (додається після запиту) */
        int offset = recv_len;

        /* Name: вказівник на ім'я у запиті (0xC00C) */
        resp_buf[offset++] = 0xC0;
        resp_buf[offset++] = 0x0C;

        /* Type: A (0x0001) */
        resp_buf[offset++] = 0x00;
        resp_buf[offset++] = 0x01;

        /* Class: IN (0x0001) */
        resp_buf[offset++] = 0x00;
        resp_buf[offset++] = 0x01;

        /* TTL: 60 секунд */
        resp_buf[offset++] = 0x00;
        resp_buf[offset++] = 0x00;
        resp_buf[offset++] = 0x00;
        resp_buf[offset++] = 0x3C;

        /* RDLENGTH = 4 (IPv4) */
        resp_buf[offset++] = 0x00;
        resp_buf[offset++] = 0x04;

        /* RDATA = 192.168.1.1 */
        resp_buf[offset++] = ap_ip[0];
        resp_buf[offset++] = ap_ip[1];
        resp_buf[offset++] = ap_ip[2];
        resp_buf[offset++] = ap_ip[3];

        lwip_sendto(sock, resp_buf, offset, 0,
                    (struct sockaddr *)&client_addr, addr_len);
    }
}

/*---------------------------------------------------------------------------
 * Callback для Wi-Fi scan
 *---------------------------------------------------------------------------*/
void wifi_scan_callback(cy_wcm_scan_result_t *result, void *user_data,
                        cy_wcm_scan_status_t status)
{
    if (status == CY_WCM_SCAN_INCOMPLETE && scan_count < MAX_SCAN_RESULTS) {
        /* Уникнення дублікатів SSID */
        bool duplicate = false;
        for (int i = 0; i < scan_count; i++) {
            if (strcmp((char *)scan_results[i].SSID, (char *)result->SSID) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && strlen((char *)result->SSID) > 0) {
            memcpy(&scan_results[scan_count], result, sizeof(cy_wcm_scan_result_t));
            scan_count++;
        }
    }

    if (status == CY_WCM_SCAN_COMPLETE) {
        scan_complete = true;
        printf("[SCAN] Завершено. Знайдено мереж: %d\n", scan_count);
    }
}

/*---------------------------------------------------------------------------
 * Генерація HTML-сторінки конфігурації з випадаючим списком мереж
 *---------------------------------------------------------------------------*/
int generate_config_page(char *buf, int buf_size)
{
    /* Початок HTML  — головна частина вбудована в index.html,
       але тут формуємо динамічний <select> з результатами scan */
    int len = 0;

    /* Генерація JSON-масиву мереж для JavaScript */
    len += snprintf(buf + len, buf_size - len,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n\r\n");

    /* HTML-сторінка конфігурації — повний вміст */
    len += snprintf(buf + len, buf_size - len,
        "<!DOCTYPE html><html lang=\"uk\"><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>PSoC6 — Налаштування Wi-Fi</title>"
        "<style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
        "min-height:100vh;display:flex;align-items:center;justify-content:center;"
        "background:linear-gradient(135deg,#0f0c29,#302b63,#24243e);color:#fff;padding:20px}"
        ".card{background:rgba(255,255,255,.08);backdrop-filter:blur(20px);"
        "border-radius:16px;padding:36px;max-width:440px;width:100%%;"
        "box-shadow:0 8px 32px rgba(0,0,0,.3);border:1px solid rgba(255,255,255,.12)}"
        "h1{text-align:center;font-size:22px;margin-bottom:6px}"
        ".sub{text-align:center;font-size:13px;color:rgba(255,255,255,.5);margin-bottom:24px}"
        "label{display:block;font-size:13px;margin-bottom:6px;color:rgba(255,255,255,.7)}"
        "select,input[type=password]{width:100%%;padding:12px;border-radius:10px;"
        "border:1px solid rgba(255,255,255,.15);background:rgba(255,255,255,.06);"
        "color:#fff;font-size:14px;margin-bottom:16px;outline:none;"
        "transition:border .2s}"
        "select:focus,input:focus{border-color:#7c5cfc}"
        "select option{background:#1a1a2e;color:#fff}"
        "button{width:100%%;padding:14px;border:none;border-radius:10px;"
        "background:linear-gradient(135deg,#7c5cfc,#a855f7);color:#fff;"
        "font-size:15px;font-weight:600;cursor:pointer;transition:transform .15s,box-shadow .2s}"
        "button:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(124,92,252,.4)}"
        ".icon{text-align:center;font-size:48px;margin-bottom:12px}"
        ".info{font-size:12px;color:rgba(255,255,255,.4);text-align:center;margin-top:16px}"
        "</style></head><body>"
        "<div class=\"card\">"
        "<div class=\"icon\">&#128225;</div>"
        "<h1>Налаштування Wi-Fi</h1>"
        "<p class=\"sub\">Богатов Ілля Сергійович &middot; КІ-408 &middot; Варіант 1</p>"
        "<form action=\"/save\" method=\"POST\">"
        "<label for=\"ssid\">Виберіть мережу Wi-Fi:</label>"
        "<select id=\"ssid\" name=\"ssid\">"
    );

    /* Динамічний список мереж */
    for (int i = 0; i < scan_count; i++) {
        int rssi = scan_results[i].signal_strength;
        const char *sec = "Open";
        if (scan_results[i].security != CY_WCM_SECURITY_OPEN)
            sec = "WPA2";
        len += snprintf(buf + len, buf_size - len,
            "<option value=\"%s\">%s (%d dBm, %s)</option>",
            (char *)scan_results[i].SSID,
            (char *)scan_results[i].SSID,
            rssi, sec);
    }

    len += snprintf(buf + len, buf_size - len,
        "</select>"
        "<label for=\"password\">Пароль:</label>"
        "<input type=\"password\" id=\"password\" name=\"password\" placeholder=\"Введіть пароль мережі\">"
        "<button type=\"submit\">&#128268; Зберегти та перезавантажити</button>"
        "</form>"
        "<p class=\"info\">Після збереження пристрій перезавантажиться та підключиться до обраної мережі.</p>"
        "</div></body></html>"
    );

    return len;
}

/*---------------------------------------------------------------------------
 * GET / — віддає конфігураційну HTML-сторінку
 *---------------------------------------------------------------------------*/
void handle_root(http_server_t srv, http_request_t req, void *user_data)
{
    char *page = (char *)malloc(8192);
    if (page == NULL) return;

    int len = generate_config_page(page, 8192);

    /* Відправити HTML-сторінку клієнту */
    // http_server_send_response(srv, req, 200, "OK", "text/html", page, len);
    printf("[HTTP] GET / → Відправлено конфігураційну сторінку (%d байт)\n", len);

    free(page);
}

/*---------------------------------------------------------------------------
 * POST /save — зберігає SSID та Password у Flash і перезавантажує
 *---------------------------------------------------------------------------*/
void handle_save(http_server_t srv, http_request_t req, void *user_data)
{
    /* 1. Отримати тіло POST-запиту (URL-encoded: ssid=...&password=...) */
    char *body = NULL;  /* http_get_body(req); */
    if (body == NULL) {
        printf("[HTTP] POST /save — порожнє тіло\n");
        return;
    }

    printf("[HTTP] POST /save — body: %s\n", body);

    /* 2. Розбір URL-encoded параметрів */
    char ssid[EEPROM_SSID_LEN]     = {0};
    char password[EEPROM_PASS_LEN] = {0};

    /* Простий парсер ssid=VALUE&password=VALUE */
    char *ssid_start = strstr(body, "ssid=");
    char *pass_start = strstr(body, "password=");

    if (ssid_start) {
        ssid_start += 5;  /* Пропустити "ssid=" */
        char *end = strchr(ssid_start, '&');
        int len = end ? (int)(end - ssid_start) : (int)strlen(ssid_start);
        if (len >= EEPROM_SSID_LEN) len = EEPROM_SSID_LEN - 1;
        strncpy(ssid, ssid_start, len);
    }

    if (pass_start) {
        pass_start += 9;  /* Пропустити "password=" */
        char *end = strchr(pass_start, '&');
        int len = end ? (int)(end - pass_start) : (int)strlen(pass_start);
        if (len >= EEPROM_PASS_LEN) len = EEPROM_PASS_LEN - 1;
        strncpy(password, pass_start, len);
    }

    /* URL-decode (заміна '+' на ' ' та '%XX' на символи) */
    /* Спрощена версія — замінюємо '+' на пробіл */
    for (int i = 0; ssid[i]; i++) if (ssid[i] == '+') ssid[i] = ' ';
    for (int i = 0; password[i]; i++) if (password[i] == '+') password[i] = ' ';

    printf("[HTTP] Збереження — SSID: \"%s\", Password: \"%s\"\n", ssid, password);

    /* 3. Зберегти у Flash (Emulated EEPROM) */
    eeprom_save_credentials(ssid, password);

    /* 4. Відповідь клієнту */
    const char *resp =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<style>"
        "body{font-family:sans-serif;display:flex;align-items:center;justify-content:center;"
        "min-height:100vh;background:linear-gradient(135deg,#0f0c29,#302b63,#24243e);color:#fff}"
        ".ok{text-align:center;animation:fadeIn .5s}"
        "@keyframes fadeIn{from{opacity:0;transform:translateY(20px)}to{opacity:1;transform:translateY(0)}}"
        "h2{margin-bottom:8px} p{color:rgba(255,255,255,.6)}"
        "</style></head><body>"
        "<div class=\"ok\"><h2>&#9989; Збережено!</h2>"
        "<p>Пристрій перезавантажується…</p></div></body></html>";

    // http_server_send_response(srv, req, 200, "OK", "text/html", resp, strlen(resp));
    printf("[HTTP] Відповідь відправлено. Перезавантаження...\n");

    /* 5. Програмне перезавантаження контролера (через 1 с) */
    cyhal_system_delay_ms(1000);
    NVIC_SystemReset();
}

/*---------------------------------------------------------------------------
 * Спроба підключення до збереженої мережі (STA-режим)
 * Повертає true при успіху
 *---------------------------------------------------------------------------*/
bool try_connect_sta(const char *ssid, const char *password)
{
    printf("[STA] Підключення до \"%s\" ...\n", ssid);

    cy_wcm_config_t wcm_cfg = { .interface = CY_WCM_INTERFACE_TYPE_STA };
    cy_wcm_init(&wcm_cfg);

    cy_wcm_connect_params_t conn = {
        .ap_credentials = {
            .security = CY_WCM_SECURITY_WPA2_AES_PSK
        }
    };
    strncpy((char *)conn.ap_credentials.SSID, ssid, sizeof(conn.ap_credentials.SSID) - 1);
    strncpy((char *)conn.ap_credentials.password, password, sizeof(conn.ap_credentials.password) - 1);

    cy_wcm_ip_address_t ip;
    cy_rslt_t result = cy_wcm_connect_ap(&conn, &ip);

    if (result == CY_RSLT_SUCCESS) {
        printf("[STA] Підключено! IP: %d.%d.%d.%d\n",
               (ip.ip.v4 >> 24) & 0xFF, (ip.ip.v4 >> 16) & 0xFF,
               (ip.ip.v4 >> 8)  & 0xFF,  ip.ip.v4        & 0xFF);
        return true;
    }

    printf("[STA] Помилка підключення до \"%s\"\n", ssid);
    cy_wcm_deinit();
    return false;
}

/*---------------------------------------------------------------------------
 * Запуск SoftAP-режиму + DNS + HTTP (Captive Portal)
 *---------------------------------------------------------------------------*/
void start_captive_portal(void)
{
    printf("\n========================================\n");
    printf("  CAPTIVE PORTAL — режим SoftAP\n");
    printf("  SSID: %s / Пароль: %s\n", AP_SSID, AP_PASSWORD);
    printf("  IP:   %s\n", AP_IP);
    printf("========================================\n\n");

    /* 1. Ініціалізація Wi-Fi у режимі AP */
    cy_wcm_config_t wcm_cfg = { .interface = CY_WCM_INTERFACE_TYPE_AP };
    cy_wcm_init(&wcm_cfg);

    cy_wcm_ap_config_t ap_cfg;
    memset(&ap_cfg, 0, sizeof(ap_cfg));
    strncpy((char *)ap_cfg.ap_credentials.SSID, AP_SSID, sizeof(ap_cfg.ap_credentials.SSID) - 1);
    strncpy((char *)ap_cfg.ap_credentials.password, AP_PASSWORD, sizeof(ap_cfg.ap_credentials.password) - 1);
    ap_cfg.ap_credentials.security = CY_WCM_SECURITY_WPA2_AES_PSK;
    ap_cfg.channel = AP_CHANNEL;

    cy_wcm_start_ap(&ap_cfg);
    printf("[AP] SoftAP запущено.\n");

    /* 2. Сканування Wi-Fi мереж (для випадаючого списку) */
    printf("[SCAN] Сканування мереж...\n");
    scan_count = 0;
    scan_complete = false;
    cy_wcm_scan_filter_t filter = {0};
    cy_wcm_start_scan(wifi_scan_callback, NULL, &filter);

    /* Чекаємо завершення сканування (макс. 10 с) */
    int timeout = 100;
    while (!scan_complete && timeout > 0) {
        cyhal_system_delay_ms(100);
        timeout--;
    }
    cy_wcm_stop_scan();

    /* 3. Запуск DNS-сервера (в окремому потоці FreeRTOS) */
    /* xTaskCreate(dns_server_task, "DNS", 4096, NULL, 3, NULL); */
    printf("[DNS] DNS-сервер запущено (всі запити → %s)\n", AP_IP);

    /* 4. Запуск HTTP-сервера */
    // http_server_init(&http_srv, HTTP_PORT);
    http_server_register_resource(http_srv, "/",     HTTP_GET,  handle_root, NULL);
    http_server_register_resource(http_srv, "/save", HTTP_POST, handle_save, NULL);
    // http_server_start(http_srv);
    printf("[HTTP] Сервер запущено на порті %d\n", HTTP_PORT);
    printf("[HTTP] Відкрийте http://%s для налаштування\n\n", AP_IP);
}

/*---------------------------------------------------------------------------
 * main()
 *---------------------------------------------------------------------------*/
int main(void)
{
    cy_rslt_t result;

    /* 1. BSP */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS) { CY_ASSERT(0); }
    __enable_irq();

    /* 2. Serial */
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                        CY_RETARGET_IO_BAUDRATE);
    printf("\n=== Лаб 4: Captive Portal (Конфігурування Wi-Fi) ===\n");
    printf("=== Богатов Ілля Сергійович · КІ-408 · Варіант 1 ===\n\n");

    /* 3. Ініціалізація Emulated EEPROM */
    cy_em_eeprom_init(&eeprom_cfg, &eeprom_ctx);
    printf("[EEPROM] Ініціалізовано (%d байт)\n", EEPROM_SIZE);

    /* 4. Логіка ініціалізації:
     *    - Спроба зчитати SSID/Password з Flash
     *    - Якщо є — підключитись як STA
     *    - Якщо немає або помилка — запуск SoftAP + Captive Portal
     */
    bool has_credentials = eeprom_read_credentials(saved_ssid, saved_password);

    if (has_credentials && strlen(saved_ssid) > 0) {
        /* Спроба підключення до збереженої мережі */
        if (try_connect_sta(saved_ssid, saved_password)) {
            printf("\n[OK] Пристрій працює у режимі STA.\n");
            printf("[OK] Інтернет-підключення активне.\n\n");

            /* Тут можна запустити основну логіку програми (сенсори, API і т.д.) */
            for (;;) {
                cyhal_system_delay_ms(1000);
            }
        }
        printf("[WARN] Не вдалося підключитися. Перехід у режим Captive Portal.\n");
    } else {
        printf("[INFO] Збережені дані Wi-Fi відсутні.\n");
    }

    /* 5. Запуск Captive Portal */
    start_captive_portal();

    /* 6. Головний цикл */
    for (;;) {
        cyhal_system_delay_ms(1000);
    }
}
