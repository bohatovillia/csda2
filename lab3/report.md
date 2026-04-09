# Звіт з лабораторної роботи №3
**Тема:** Візуалізація сенсорних даних у реальному часі через WebSockets

**Мета роботи:** Опанувати протокол WebSockets для створення постійного з'єднання між мікроконтролером та браузером, навчитися ефективно передавати дані сенсорів та візуалізувати їх динамічно.

**Студент:** Богатов Ілля Сергійович
**Група:** КІ-408
**Варіант:** 1 — MPU-6050, Акселерометр (X, Y, Z)

---

## 1. Конфігурація сенсора та схема підключення

**Сенсор:** MPU-6050 (3-осьовий акселерометр + гіроскоп)
**Інтерфейс:** I2C (SDA, SCL)
**Адреса:** 0x68

```
┌────────────────────┐         I2C          ┌──────────┐
│ CY8CKIT-062-WIFI-BT│◄───── SDA/SCL ─────►│ MPU-6050 │
│                    │                      │          │
│   Wi-Fi (STA)      │        TCP/81        │ ACCEL    │
│   HTTP  :80        │◄═══════════════════► │ X, Y, Z  │
│   WS    :81        │     WebSocket        └──────────┘
└────────┬───────────┘
         │ Wi-Fi
         ▼
┌────────────────┐
│    Браузер     │
│  Chart.js      │
│  WebSocket     │
│  onmessage()   │
└────────────────┘
```

**Параметри I2C:**
- Частота: 100 кГц
- SDA: `CYBSP_I2C_SDA`
- SCL: `CYBSP_I2C_SCL`

**Параметри MPU-6050:**
- Діапазон акселерометра: ±2g
- Чутливість: 16384 LSB/g
- Регістр PWR_MGMT_1 (0x6B) → 0x00 (вивід зі сну)
- Регістр ACCEL_XOUT_H (0x3B) → зчитування 6 байт (X, Y, Z)

---

## 2. Лістинг коду

### 2.1 Ініціалізація MPU-6050 (I2C)

```c
void mpu6050_init(void)
{
    cyhal_i2c_cfg_t i2c_cfg = {
        .is_slave        = false,
        .address         = 0,
        .frequencyhal_hz = 100000
    };
    cyhal_i2c_init(&i2c_obj, CYBSP_I2C_SDA, CYBSP_I2C_SCL, NULL);
    cyhal_i2c_configure(&i2c_obj, &i2c_cfg);

    /* Вивести MPU-6050 зі сну */
    uint8_t wake_cmd[2] = { 0x6B, 0x00 };
    cyhal_i2c_master_write(&i2c_obj, 0x68, wake_cmd, 2, 100, true);
}
```

### 2.2 Зчитування акселерометра

```c
void read_accelerometer(float *x, float *y, float *z)
{
    uint8_t reg = 0x3B;
    uint8_t buf[6];

    cyhal_i2c_master_write(&i2c_obj, 0x68, &reg, 1, 100, false);
    cyhal_i2c_master_read(&i2c_obj, 0x68, buf, 6, 100, true);

    int16_t raw_x = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t raw_y = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t raw_z = (int16_t)((buf[4] << 8) | buf[5]);

    *x = raw_x / 16384.0f;
    *y = raw_y / 16384.0f;
    *z = raw_z / 16384.0f;
}
```

### 2.3 Відправка даних через WebSocket

```c
void sensor_timer_callback(void *arg)
{
    float x, y, z;
    read_accelerometer(&x, &y, &z);

    char buffer[128];
    snprintf(buffer, sizeof(buffer),
             "{\"x\":%.2f, \"y\":%.2f, \"z\":%.2f}", x, y, z);

    ws_server_send_text_all(&ws_srv, buffer, strlen(buffer));
    printf("[WS TX] %s\n", buffer);
}
```

### 2.4 Реєстрація серверів

```c
    /* HTTP-сервер (порт 80) — роздача index.html */
    http_server_init(&http_srv, 80);
    http_server_register_resource(http_srv, "/", HTTP_GET, handle_root, NULL);
    http_server_start(http_srv);

    /* WebSocket-сервер (порт 81) */
    ws_server_init(&ws_srv, 81);
    ws_server_start(&ws_srv);
```

### 2.5 Frontend — WebSocket клієнт + Chart.js

```javascript
const socket = new WebSocket('ws://' + location.hostname + ':81');

socket.onmessage = function(event) {
    const data = JSON.parse(event.data);  // {"x":0.12, "y":-0.05, "z":0.98}

    // Додати нову точку на графік
    chart.data.labels.push(new Date().toLocaleTimeString());
    chart.data.datasets[0].data.push(data.x);
    chart.data.datasets[1].data.push(data.y);
    chart.data.datasets[2].data.push(data.z);

    // Rolling window — видалити старі точки (макс. 30)
    if (chart.data.labels.length > 30) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
        chart.data.datasets[1].data.shift();
        chart.data.datasets[2].data.shift();
    }

    chart.update('none');  // Без анімації для швидкості
};
```

---

## 3. Скриншот Serial-логу

*[ ВСТАВТЕ СЮДИ СКРИНШОТ SERIAL-ТЕРМІНАЛУ ]*

> **Очікуваний вивід:**
> ```
> === Лаб 3: WebSocket + MPU-6050 ===
> MPU-6050 ініціалізовано (I2C: 0x68)
> Ініціалізація Wi-Fi...
> Підключення до MyWiFi ...
> Підключено! IP: 192.168.1.100
> LittleFS OK.
> HTTP-сервер (порт 80) OK.
> WebSocket-сервер (порт 81) OK.
> Таймер сенсора: кожні 500 мс.
> [WS TX] {"x":0.12, "y":-0.05, "z":0.98}
> [WS TX] {"x":0.15, "y":-0.03, "z":1.01}
> ```

---

## 4. Скриншот браузера з графіком

*[ ВСТАВТЕ СЮДИ СКРИНШОТ БРАУЗЕРА ]*

> **Опис:** Dashboard показує:
> - Поточні значення X, Y, Z у реальному часі
> - Лінійний графік (Chart.js) з трьома лініями (червона — X, зелена — Y, синя — Z)
> - Rolling window (30 точок) — графік плавно зсувається
> - Статус WebSocket-з'єднання та лічильник пакетів
> - Лог WebSocket-повідомлень у стилі терміналу

---

## 5. Контрольні запитання

**1. У чому полягає перевага WebSocket над HTTP Long Polling для вбудованих систем?**

WebSocket встановлює одне постійне TCP-з'єднання з мінімальним overhead (2–14 байт на фрейм), тоді як Long Polling вимагає повторного встановлення TCP-з'єднання та передачі повних HTTP-заголовків (сотні байт) при кожному опитуванні. Для мікроконтролерів із обмеженою пам'яттю та слабким мережевим стеком (lwIP) це суттєво зменшує навантаження на RAM та CPU, а також забезпечує значно нижчу затримку доставки даних.

**2. Який обсяг службової інформації (overhead) має фрейм WebSocket порівняно з HTTP-запитом?**

WebSocket-фрейм має мінімальний overhead: 2 байти для коротких повідомлень (до 125 байт payload), 4 байти для середніх (до 65535 байт) і 10 байт для великих. Плюс 4 байти маски від клієнта. Типовий HTTP-запит/відповідь містить 200–800 байт заголовків (метод, URL, Host, Content-Type, Cookie тощо). Для IoT-пакета розміром 50 байт WebSocket дає overhead ~10%, тоді як HTTP — до 1000%.

**3. Чому важливо обмежувати кількість точок на графіку в браузері при тривалому моніторингу?**

Кожна точка даних зберігається у масиві в пам'яті браузера (RAM). При частоті 2 Гц за годину накопичується ~7200 точок на кожну вісь. Необмежений масив призводить до зростання використання пам'яті, уповільнення рендерингу Canvas та "зависання" вкладки. Техніка rolling window (наприклад, 30 точок) зберігає лише актуальні дані, забезпечуючи стабільну продуктивність при тривалому моніторингу.

**4. Як впливає частота дискретизації сенсора на стабільність роботи мережевого стека lwIP?**

Висока частота дискретизації (наприклад, 100+ Гц) генерує великий потік пакетів, що може переповнити буфери передачі lwIP (pbuf pool), спричинити затримки у TCP-стеку та навіть втрату з'єднання. Мікроконтролер з одним ядром для обробки мережевого стека може не встигати обробляти і дані сенсора, і мережеві події. Рекомендовано обирати частоту, яка не перевищує пропускну здатність каналу (для WiFi — зазвичай 10–50 Гц достатньо).

---

## Список використаних джерел

1. RFC 6455 — The WebSocket Protocol: [tools.ietf.org](https://tools.ietf.org/html/rfc6455)
2. Chart.js Documentation: [chartjs.org](https://www.chartjs.org/docs/latest/)
3. MDN Web Docs — WebSockets API: [developer.mozilla.org](https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API)
4. MPU-6050 Datasheet: [invensense.tdk.com](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Datasheet1.pdf)
