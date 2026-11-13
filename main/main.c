/**
 * @file main.c
 * @brief Nodo IoT ESP32-S3: SHT3x + DS3231, deteccion de anomalias y OTA.
 *
 * Lee temperatura y humedad del sensor SHT3x por I2C, obtiene la fecha y hora
 * del reloj DS3231 y detecta anomalias termicas con un z-score sobre una
 * ventana movil, combinado con la tasa de cambio de la temperatura.
 *
 * Ademas conecta a Wi-Fi y permite actualizar el firmware por HTTPS (OTA)
 * con una pulsacion larga del boton BOOT. Si la imagen nueva no logra
 * arrancar y conectarse, el bootloader vuelve a la version anterior (rollback).
 */
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ota_manager.h"

static const char *TAG = "NODE";

#define PIN_SDA        8      /* TODO: confirmar con el esquematico */
#define PIN_SCL        9      /* TODO: confirmar con el esquematico */
#define SHT3X_ADDR     0x44   /* 0x45 si el pin ADDR va a 3V3 */
#define DS3231_ADDR    0x68
#define WIN_SIZE       30
#define Z_THRESHOLD    3.0f
#define DTDT_THRESHOLD 1.0f   /* grados C por segundo */

#define PIN_BOOT_BTN   0      /* Boton BOOT de la tarjeta (GPIO0, activo en bajo) */
#define OTA_HOLD_MS    1000   /* Pulsacion larga para disparar el OTA */

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t sht, rtc;

/** @brief CRC-8 (polinomio 0x31, inicial 0xFF) usado por el SHT3x. */
static uint8_t sht_crc(const uint8_t *d, int n) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
    return crc;
}

/**
 * @brief Lee temperatura y humedad (disparo unico, alta repetibilidad).
 * @param[out] t Temperatura en grados C
 * @param[out] h Humedad relativa en %
 * @return ESP_OK si la lectura y el CRC son validos
 */
static esp_err_t sht_read(float *t, float *h) {
    const uint8_t cmd[2] = {0x24, 0x00};
    uint8_t r[6];
    esp_err_t e = i2c_master_transmit(sht, cmd, 2, 100);
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(20));
    e = i2c_master_receive(sht, r, 6, 100);
    if (e != ESP_OK) return e;
    if (sht_crc(r, 2) != r[2] || sht_crc(r + 3, 2) != r[5]) return ESP_ERR_INVALID_CRC;
    *t = -45.0f + 175.0f * ((r[0] << 8) | r[1]) / 65535.0f;
    *h = 100.0f * ((r[3] << 8) | r[4]) / 65535.0f;
    return ESP_OK;
}

/** @brief Convierte un valor BCD a decimal. */
static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

/**
 * @brief Lee fecha y hora del DS3231.
 * @param[out] s Cadena "AAAA-MM-DD HH:MM:SS"
 * @param len Tamano del buffer (minimo 20 bytes)
 * @return ESP_OK si la lectura fue correcta
 */
static esp_err_t rtc_read(char *s, size_t len) {
    uint8_t reg = 0x00, d[7];
    esp_err_t e = i2c_master_transmit_receive(rtc, &reg, 1, d, 7, 100);
    if (e != ESP_OK) return e;
    snprintf(s, len, "20%02d-%02d-%02d %02d:%02d:%02d",
             bcd2dec(d[6]), bcd2dec(d[5] & 0x1F), bcd2dec(d[4]),
             bcd2dec(d[2] & 0x3F), bcd2dec(d[1]), bcd2dec(d[0]));
    return ESP_OK;
}

/** @brief Estado de la ventana movil para el z-score. */
typedef struct {
    float buf[WIN_SIZE]; /**< Muestras recientes de temperatura */
    int n;               /**< Cantidad de muestras validas */
    int idx;             /**< Posicion de escritura circular */
    float last;          /**< Ultima muestra */
} window_t;

/**
 * @brief Detector de anomalias: z-score en ventana movil + tasa de cambio.
 * @param w Ventana de muestras
 * @param x Nueva muestra de temperatura
 * @param dt Segundos desde la muestra anterior
 * @param[out] z Z-score calculado
 * @return true si se detecta una anomalia
 */
static bool detect(window_t *w, float x, float dt, float *z) {
    bool alert = false;
    *z = 0;
    if (w->n >= 10) {
        float m = 0, v = 0;
        for (int i = 0; i < w->n; i++) m += w->buf[i];
        m /= w->n;
        for (int i = 0; i < w->n; i++) v += (w->buf[i] - m) * (w->buf[i] - m);
        float sd = sqrtf(v / w->n);
        if (sd < 0.05f) sd = 0.05f;           /* evita division por casi cero */
        *z = (x - m) / sd;
        float rate = (x - w->last) / dt;
        alert = fabsf(*z) > Z_THRESHOLD || fabsf(rate) > DTDT_THRESHOLD;
    }
    w->buf[w->idx] = x;
    w->idx = (w->idx + 1) % WIN_SIZE;
    if (w->n < WIN_SIZE) w->n++;
    w->last = x;
    return alert;
}

/** @brief Tarea principal: muestrea cada 1 s y reporta alertas con la hora del RTC. */
static void sensor_task(void *arg) {
    window_t w = {0};
    char ts[24] = "--";
    float t, h, z;
    while (1) {
        if (sht_read(&t, &h) == ESP_OK) {
            bool a = detect(&w, t, 1.0f, &z);
            rtc_read(ts, sizeof(ts));
            ESP_LOGI(TAG, "[%s] T=%.2f C H=%.1f %% z=%.2f", ts, t, h, z);
            if (a) ESP_LOGW(TAG, "ALERTA ANOMALIA TERMICA [%s] T=%.2f z=%.2f", ts, t, z);
        } else {
            ESP_LOGE(TAG, "Fallo lectura SHT3x");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Tarea del boton BOOT: una pulsacion larga (1 s) inicia el OTA.
 * @param arg No se usa
 */
static void button_task(void *arg) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);

    int held_ms = 0;
    while (1) {
        if (gpio_get_level(PIN_BOOT_BTN) == 0) {
            held_ms += 50;
            if (held_ms == OTA_HOLD_MS) {
                ESP_LOGI(TAG, "Pulsacion larga: iniciando OTA");
                ota_perform_update();
            }
        } else {
            held_ms = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Punto de entrada: NVS, I2C, sensores, Wi-Fi, validacion OTA y boton.
 */
void app_main(void) {
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    ota_log_version();

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));

    for (uint8_t a = 0x08; a < 0x78; a++)           /* escaner I2C */
        if (i2c_master_probe(bus, a, 50) == ESP_OK)
            ESP_LOGI(TAG, "Dispositivo I2C en 0x%02X", a);

    i2c_device_config_t d = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .scl_speed_hz = 100000};
    d.device_address = SHT3X_ADDR;  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &d, &sht));
    d.device_address = DS3231_ADDR; ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &d, &rtc));

    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);

    if (wifi_connect() == ESP_OK) {
        ota_mark_valid();   /* la imagen arranco y tiene red: se acepta */
    } else {
        ESP_LOGW(TAG, "Sin Wi-Fi: el OTA no estara disponible");
    }

    xTaskCreate(button_task, "button", 8192, NULL, 4, NULL);
}