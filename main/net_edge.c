/**
 * @file net_edge.c
 * @brief Nodo-Edge: envia lecturas por HTTPS (POST /api/lectura) y mide metricas.
 *
 * Una tarea consume una cola de lecturas y las envia al servidor edge. En cada
 * envio mide el RTT (tiempo de ida y vuelta) y lleva la cuenta de entregas
 * exitosas para calcular la tasa de entrega.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "net_edge.h"
#include "wifi_secrets.h"

static const char *TAG = "EDGE";

/** URL del endpoint. Se puede sobreescribir en wifi_secrets.h. */
#ifndef EDGE_URL
#define EDGE_URL "https://" SERVER_IP ":8070/api/lectura"
#endif

#define EDGE_QUEUE_LEN 8

/** Certificado del servidor, incrustado desde server_certs/ca_cert.pem. */
extern const uint8_t ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t ca_cert_pem_end[]   asm("_binary_ca_cert_pem_end");

static QueueHandle_t s_queue;
static uint32_t s_sent;       /**< Envios intentados */
static uint32_t s_ok;         /**< Envios confirmados por el servidor (HTTP 200) */
static int      s_last_rtt;   /**< RTT del envio anterior, en ms */

/**
 * @brief Envia una lectura al servidor y mide el RTT.
 * @param r Lectura a enviar
 * @return ESP_OK si el servidor respondio HTTP 200
 */
static esp_err_t edge_post(const edge_reading_t *r) {
    char body[192];
    int n = snprintf(body, sizeof(body),
        "{\"ts\":\"%s\",\"t\":%.2f,\"h\":%.1f,\"z\":%.2f,\"alert\":%s,\"rtt_ms\":%d}",
        r->ts, r->temp_c, r->hum_rh, r->z, r->alert ? "true" : "false", s_last_rtt);
    if (n <= 0 || n >= (int)sizeof(body)) return ESP_ERR_INVALID_SIZE;

    esp_http_client_config_t cfg = {
        .url = EDGE_URL,
        .method = HTTP_METHOD_POST,
        .cert_pem = (const char *)ca_cert_pem_start,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;

    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, body, n);

    int64_t t0 = esp_timer_get_time();
    esp_err_t e = esp_http_client_perform(c);
    s_last_rtt = (int)((esp_timer_get_time() - t0) / 1000);

    int status = (e == ESP_OK) ? esp_http_client_get_status_code(c) : -1;
    esp_http_client_cleanup(c);

    if (e == ESP_OK && status == 200) return ESP_OK;
    ESP_LOGW(TAG, "Envio fallido: err=%s http=%d", esp_err_to_name(e), status);
    return ESP_FAIL;
}

/** @brief Tarea: toma lecturas de la cola y las envia si hay Wi-Fi. */
static void edge_task(void *arg) {
    edge_reading_t r;
    wifi_ap_record_t ap;
    while (1) {
        if (xQueueReceive(s_queue, &r, portMAX_DELAY) != pdTRUE) continue;

        s_sent++;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
            ESP_LOGW(TAG, "Sin Wi-Fi: lectura descartada");
            continue;
        }
        if (edge_post(&r) == ESP_OK) s_ok++;

        ESP_LOGI(TAG, "RTT=%d ms | entregados %u/%u (%.0f %%)",
                 s_last_rtt, (unsigned)s_ok, (unsigned)s_sent,
                 100.0f * s_ok / s_sent);
    }
}

esp_err_t net_edge_init(void) {
    s_queue = xQueueCreate(EDGE_QUEUE_LEN, sizeof(edge_reading_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(edge_task, "edge", 8192, NULL, 3, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Nodo-edge listo. Destino: %s", EDGE_URL);
    return ESP_OK;
}

bool net_edge_publish(const edge_reading_t *r) {
    if (!s_queue) return false;
    return xQueueSend(s_queue, r, 0) == pdTRUE;
}