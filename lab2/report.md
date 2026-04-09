# Звіт з лабораторної роботи №2
**Тема:** Асинхронне керування периферією через Web Interface

**Мета роботи:** Опанувати технологію асинхронних запитів (AJAX/Fetch) для взаємодії з апаратним забезпеченням мікроконтролера, навчитися проектувати REST-подібні ендпоінти та працювати з форматом JSON на вбудованих системах.

**Студент:** Богатов Ілля Сергійович  
**Група:** КІ-408  
**Варіант:** 1

---

## 1. Схема взаємодії та архітектура системи

Система побудована за клієнт-серверною архітектурою:

```
┌──────────────┐    Wi-Fi     ┌─────────────────────────┐
│   Браузер    │◄────────────►│  CY8CKIT-062-WIFI-BT    │
│  (Frontend)  │   HTTP/JSON  │       (Backend)          │
│              │              │                          │
│  index.html  │  GET /       │  HTTP Server (порт 80)   │
│  fetch()     │  GET /api/   │  LittleFS               │
│  polling 5s  │  POST /api/  │  cJSON + GPIO            │
└──────────────┘              └─────────────┬────────────┘
                                            │
                                       ┌────▼────┐
                                       │  LED    │
                                       │(GPIO)   │
                                       └─────────┘
```

**REST API ендпоінти:**

| Метод | URL             | Опис                                     | Тіло запиту                  | Тіло відповіді           |
|-------|-----------------|------------------------------------------|------------------------------|--------------------------|
| GET   | `/`             | Віддає Dashboard (index.html) з LittleFS | —                            | HTML-сторінка             |
| GET   | `/api/status`   | Поточний стан LED                        | —                            | `{"led_on": true/false}` |
| POST  | `/api/control`  | Керування LED                            | `{"command": "on/off/toggle"}` | `{"result": "ok"}`       |

---

## 2. Лістинг коду

### 2.1 Backend — обробка JSON на контролері (C)

**Обробник GET /api/status:**

```c
void handle_get_status(http_server_t srv, http_request_t req, void *user_data)
{
    char response[64];
    snprintf(response, sizeof(response),
             "{\"led_on\": %s}", g_led_state ? "true" : "false");

    printf("[GET /api/status] -> %s\n", response);
    http_server_send_response(srv, req, 200, "OK",
                              "application/json", response, strlen(response));
}
```

**Обробник POST /api/control:**

```c
void handle_post_control(http_server_t srv, http_request_t req, void *user_data)
{
    char *body = http_get_body(req);
    if (body == NULL) {
        http_server_send_response(srv, req, 400, "Bad Request",
            "application/json", "{\"error\":\"empty body\"}", 21);
        return;
    }

    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        printf("[POST /api/control] Помилка парсингу JSON\n");
        http_server_send_response(srv, req, 400, "Bad Request",
            "application/json", "{\"error\":\"invalid json\"}", 23);
        return;
    }

    cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(json, "command");
    if (cJSON_IsString(cmd_item) && cmd_item->valuestring != NULL) {
        const char *cmd = cmd_item->valuestring;

        if (strcmp(cmd, "on") == 0) {
            g_led_state = true;
            cyhal_gpio_write(CYBSP_USER_LED, false);
        } else if (strcmp(cmd, "off") == 0) {
            g_led_state = false;
            cyhal_gpio_write(CYBSP_USER_LED, true);
        } else if (strcmp(cmd, "toggle") == 0) {
            g_led_state = !g_led_state;
            cyhal_gpio_toggle(CYBSP_USER_LED);
        }

        printf("[POST /api/control] Команда: %s -> LED = %s\n",
               cmd, g_led_state ? "ON" : "OFF");
    }

    cJSON_Delete(json);
    http_server_send_response(srv, req, 200, "OK",
        "application/json", "{\"result\":\"ok\"}", 15);
}
```

**Реєстрація ендпоінтів та запуск HTTP-сервера:**

```c
    http_server_init(&server, 80);

    http_server_register_resource(server, "/",            HTTP_GET,  handle_root,         NULL);
    http_server_register_resource(server, "/api/status",  HTTP_GET,  handle_get_status,   NULL);
    http_server_register_resource(server, "/api/control", HTTP_POST, handle_post_control, NULL);

    http_server_start(server);
    printf("HTTP-сервер запущено на порті 80.\n");
```

### 2.2 Frontend — асинхронна логіка (JavaScript)

**Функція надсилання команди (Fetch API, POST):**

```javascript
async function sendCommand(cmd) {
    const payload = { command: cmd };
    log('POST /api/control  →  ' + JSON.stringify(payload), 'info');

    const response = await fetch('/api/control', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    });
    const data = await response.json();

    log('← 200 OK ' + JSON.stringify(data), 'ok');
    log('LED → ' + (ledState ? 'ON' : 'OFF'), ledState ? 'ok' : 'warn');
    updateUI();
}
```

**Функція polling (Fetch API, GET) — автоматичне оновлення кожні 5 секунд:**

```javascript
async function fetchStatus() {
    log('GET /api/status', 'info');
    const response = await fetch('/api/status');
    const data = await response.json();    // {"led_on": true/false}
    ledState = data.led_on;
    log('← 200 OK ' + JSON.stringify(data), 'ok');
    updateUI();
}

// Polling кожні 5 секунд
setInterval(fetchStatus, 5000);
```

---

## 3. Скриншот Serial-логу з IP-адресою пристрою

*[ ВСТАВТЕ СЮДИ СКРИНШОТ SERIAL-ТЕРМІНАЛУ ]*

> **Очікуваний вивід:**
> ```
> === Лаб 2: Асинхронне керування периферією ===
> Ініціалізація Wi-Fi...
> Підключення до MyWiFi ...
> Підключено! IP-адреса: 192.168.1.100
> Монтування LittleFS...
> LittleFS OK.
> HTTP-сервер запущено на порті 80. Очікування запитів...
> [GET /api/status] -> {"led_on": false}
> [POST /api/control] Команда: on -> LED = ON
> ```

---

## 4. Скриншот браузера з відображеною сторінкою

*[ ВСТАВТЕ СЮДИ СКРИНШОТ БРАУЗЕРА ]*

> **Опис:** На сторінці Dashboard відображаються:
> - Інформація про пристрій та ендпоінти
> - Круглий індикатор LED (зелений = ON, сірий = OFF)
> - Кнопки ON / OFF для керування
> - Системний лог у стилі терміналу з кольоровими записами всіх запитів/відповідей

---

## 5. Контрольні запитання

**1. Чому використання JSON є вигіднішим за передачу «сирого» тексту в IoT?**

JSON забезпечує структуровану передачу даних із чітким іменуванням полів, що спрощує парсинг на обох сторонах. На відміну від «сирого» тексту, JSON стандартизований і має готові бібліотеки для роботи як у браузері (вбудований `JSON.parse()`), так і на мікроконтролері (cJSON, ArduinoJson). Це зменшує ймовірність помилок при обробці та дозволяє легко розширювати протокол, додаючи нові поля без порушення сумісності.

**2. Чим метод POST відрізняється від GET при проектуванні API для керування залізом?**

GET є ідемпотентним і використовується виключно для читання стану (наприклад, `GET /api/status`), він не повинен змінювати стан системи. POST використовується для дій, що змінюють стан (наприклад, увімкнення LED через `POST /api/control`). Крім того, GET передає параметри через URL, а POST — через тіло запиту, що дозволяє надсилати складніші структури даних і є безпечнішим варіантом.

**3. Які переваги Fetch API над застарілим XMLHttpRequest?**

Fetch API повертає Promise, що дозволяє використовувати async/await і писати чистіший, читабельніший код. Він має простіший синтаксис, вбудовану підтримку роботи з JSON (`response.json()`), підтримку потокового читання (Streams API) та краще оброблення помилок. XMLHttpRequest вимагає використання callback-функцій і має більш громіздкий API.

**4. Як обробка JSON впливає на використання оперативної пам'яті (RAM) мікроконтролера?**

Парсинг JSON за допомогою cJSON створює дерево об'єктів у динамічній пам'яті (heap), що може суттєво збільшити використання RAM. Для мікроконтролерів із обмеженою пам'яттю (наприклад, 256–512 КБ RAM у PSoC 6) важливо мінімізувати розмір JSON-повідомлень, уникати глибокої вкладеності, та обов'язково звільняти пам'ять через `cJSON_Delete()` одразу після обробки.

---

## Список використаних джерел

1. Fetch API Guide — MDN Web Docs: [developer.mozilla.org](https://developer.mozilla.org/en-US/docs/Web/API/Fetch_API)
2. cJSON Library for Embedded Systems: [github.com/DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)
3. Infineon AnyCloud HTTP Server Example: [github.com/Infineon](https://github.com/Infineon/mtb-example-anycloud-https-server)
4. REST API Design Guide for IoT: [restfulapi.net](https://restfulapi.net)
