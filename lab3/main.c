#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cy_wcm.h"
#include "lfs.h"
#include "http_server.h"
#include "ws_server.h"      /* WebSocket server API */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*---------------------------------------------------------------------------
 * Конфігурація
 *---------------------------------------------------------------------------*/
#define WIFI_SSID        "Ваш_SSID"
#define WIFI_PASSWORD    "Ваш_Пароль"

/* I2C адреса MPU-6050 */
#define MPU6050_ADDR     0x68
#define MPU6050_REG_PWR  0x6B
#define MPU6050_REG_ACCEL 0x3B

/* Інтервал зчитування сенсора (мс) */
#define SENSOR_INTERVAL_MS  500

/*---------------------------------------------------------------------------
 * Глобальні змінні
 *---------------------------------------------------------------------------*/
lfs_t lfs;
struct lfs_config cfg;
http_server_t http_srv;
ws_server_t   ws_srv;

/* I2C об'єкт */
cyhal_i2c_t i2c_obj;

/*---------------------------------------------------------------------------
 * Ініціалізація I2C та MPU-6050
 *---------------------------------------------------------------------------*/
void mpu6050_init(void)
{
    /* Налаштування I2C (100 кГц) */
    cyhal_i2c_cfg_t i2c_cfg = {
        .is_slave        = false,
        .address         = 0,
        .frequencyhal_hz = 100000
    };
    cyhal_i2c_init(&i2c_obj, CYBSP_I2C_SDA, CYBSP_I2C_SCL, NULL);
    cyhal_i2c_configure(&i2c_obj, &i2c_cfg);

    /* Вивести MPU-6050 зі сну: записати 0x00 у регістр PWR_MGMT_1 (0x6B) */
    uint8_t wake_cmd[2] = { MPU6050_REG_PWR, 0x00 };
    cyhal_i2c_master_write(&i2c_obj, MPU6050_ADDR, wake_cmd, 2, 100, true);

    printf("MPU-6050 ініціалізовано (I2C: 0x%02X)\n", MPU6050_ADDR);
}

/*---------------------------------------------------------------------------
 * Зчитування акселерометра (X, Y, Z) у одиницях g
 *---------------------------------------------------------------------------*/
void read_accelerometer(float *x, float *y, float *z)
{
    uint8_t reg = MPU6050_REG_ACCEL;
    uint8_t buf[6];

    /* Записати адресу регістра, потім прочитати 6 байт (ACCEL_XOUT_H..ACCEL_ZOUT_L) */
    cyhal_i2c_master_write(&i2c_obj, MPU6050_ADDR, &reg, 1, 100, false);
    cyhal_i2c_master_read(&i2c_obj, MPU6050_ADDR, buf, 6, 100, true);

    /* Зібрати 16-бітні значення (big-endian) */
    int16_t raw_x = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t raw_y = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t raw_z = (int16_t)((buf[4] << 8) | buf[5]);

    /* Перетворення у g (діапазон ±2g, чутливість 16384 LSB/g) */
    *x = raw_x / 16384.0f;
    *y = raw_y / 16384.0f;
    *z = raw_z / 16384.0f;
}

/*---------------------------------------------------------------------------
 * Callback таймера — зчитує сенсор і відправляє JSON через WebSocket
 *---------------------------------------------------------------------------*/
void sensor_timer_callback(void *arg)
{
    float x, y, z;
    read_accelerometer(&x, &y, &z);

    char buffer[128];
    snprintf(buffer, sizeof(buffer),
             "{\"x\":%.2f, \"y\":%.2f, \"z\":%.2f}", x, y, z);

    /* Відправка всім підключеним WebSocket-клієнтам */
    ws_server_send_text_all(&ws_srv, buffer, strlen(buffer));

    printf("[WS TX] %s\n", buffer);
}

/*---------------------------------------------------------------------------
 * GET /  — віддає index.html з LittleFS
 *---------------------------------------------------------------------------*/
void handle_root(http_server_t srv, http_request_t req, void *user_data)
{
    lfs_file_t file;
    int err = lfs_file_open(&lfs, &file, "index.html", LFS_O_RDONLY);
    if (err < 0) { return; }

    lfs_ssize_t size = lfs_file_size(&lfs, &file);
    char *buf = (char *)malloc(size + 1);
    if (buf) {
        lfs_file_read(&lfs, &file, buf, size);
        buf[size] = '\0';
        // http_server_send_response(srv, req, 200, "OK", "text/html", buf, size);
        free(buf);
    }
    lfs_file_close(&lfs, &file);
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
    printf("=== Лаб 3: WebSocket + MPU-6050 ===\n");

    /* 3. MPU-6050 (I2C) */
    mpu6050_init();

    /* 4. Wi-Fi */
    printf("Ініціалізація Wi-Fi...\n");
    cy_wcm_config_t wcm_cfg = { .interface = CY_WCM_INTERFACE_TYPE_STA };
    cy_wcm_init(&wcm_cfg);

    cy_wcm_connect_params_t conn = {
        .ap_credentials = {
            .SSID     = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .security = CY_WCM_SECURITY_WPA2_AES_PSK
        }
    };
    cy_wcm_ip_address_t ip;
    printf("Підключення до %s ...\n", WIFI_SSID);

    if (cy_wcm_connect_ap(&conn, &ip) == CY_RSLT_SUCCESS) {
        printf("Підключено! IP: %d.%d.%d.%d\n",
               (ip.ip.v4 >> 24) & 0xFF, (ip.ip.v4 >> 16) & 0xFF,
               (ip.ip.v4 >> 8)  & 0xFF,  ip.ip.v4        & 0xFF);
    } else {
        printf("Wi-Fi FAIL\n");
        while (1) {}
    }

    /* 5. LittleFS */
    int err = lfs_mount(&lfs, &cfg);
    if (err) { lfs_format(&lfs, &cfg); lfs_mount(&lfs, &cfg); }
    printf("LittleFS OK.\n");

    /* 6. HTTP-сервер (порт 80) — для роздачі index.html */
    // http_server_init(&http_srv, 80);
    http_server_register_resource(http_srv, "/", HTTP_GET, handle_root, NULL);
    // http_server_start(http_srv);
    printf("HTTP-сервер (порт 80) OK.\n");

    /* 7. WebSocket-сервер (порт 81) */
    // ws_server_init(&ws_srv, 81);
    // ws_server_start(&ws_srv);
    printf("WebSocket-сервер (порт 81) OK.\n");

    /* 8. Таймер — зчитування сенсора кожні SENSOR_INTERVAL_MS мс */
    cyhal_timer_t sensor_timer;
    // Налаштувати апаратний або програмний таймер з інтервалом SENSOR_INTERVAL_MS
    // та callback sensor_timer_callback
    printf("Таймер сенсора: кожні %d мс.\n", SENSOR_INTERVAL_MS);

    /* Головний цикл */
    for (;;) {
        cyhal_system_delay_ms(1000);
    }
}
