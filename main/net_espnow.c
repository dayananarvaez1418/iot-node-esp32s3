/**
 * @file net_espnow.c
 * @brief Nodo-Nodo: intercambio de lecturas entre placas por ESP-NOW.
 *
 * Cada nodo transmite su lectura en broadcast y escucha las de los demas.
 * Cada mensaje lleva un numero de secuencia: al recibir, un salto en la
 * secuencia indica mensajes perdidos, con lo que se calcula la tasa de entrega.
 *
 * Nota: ambos nodos deben estar en el mismo canal Wi-Fi. Si los dos se conectan
 * al mismo hotspot, el canal coincide automaticamente. Los mensajes en broadcast
 * no van cifrados.
 */
#include <string.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "net_espnow.h"

static const char *TAG = "ESPNOW";

#define MSG_MAGIC 0xA5

/** @brief Mensaje que viaja entre nodos (formato compacto, 14 bytes). */
typedef struct __attribute__((packed)) {
    uint8_t  magic;     /**< Marca para ignorar paquetes ajenos */
    uint8_t  node_id;   /**< Ultimo byte de la MAC del emisor */
    uint16_t seq;       /**< Numero de secuencia del emisor */
    float    temp_c;    /**< Temperatura en grados C */
    float    hum_rh;    /**< Humedad relativa en % */
    uint8_t  alert;     /**< 1 si el emisor detecto una anomalia */
} espnow_msg_t;

static const uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool     s_ready;
static uint8_t  s_node_id;
static uint16_t s_seq;          /**< Secuencia propia (siguiente a enviar) */
static uint32_t s_tx;           /**< Mensajes enviados */
static uint32_t s_rx;           /**< Mensajes recibidos de vecinos */
static uint32_t s_lost;         /**< Mensajes perdidos estimados por saltos de secuencia */
static int32_t  s_last_rx_seq = -1;

/**
 * @brief Callback de recepcion: valida el mensaje y actualiza las estadisticas.
 * @param info Datos del paquete (incluye la MAC del emisor)
 * @param data Contenido recibido
 * @param len Tamano del contenido
 */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != (int)sizeof(espnow_msg_t)) return;

    espnow_msg_t m;
    memcpy(&m, data, sizeof(m));
    if (m.magic != MSG_MAGIC || m.node_id == s_node_id) return;

    if (s_last_rx_seq >= 0 && m.seq > s_last_rx_seq + 1)
        s_lost += m.seq - s_last_rx_seq - 1;
    s_last_rx_seq = m.seq;
    s_rx++;

    ESP_LOGI(TAG, "Vecino %02X: T=%.2f C H=%.1f %% alerta=%u | recibidos %u, perdidos %u (%.0f %% entregados)",
             m.node_id, m.temp_c, m.hum_rh, m.alert,
             (unsigned)s_rx, (unsigned)s_lost,
             100.0f * s_rx / (s_rx + s_lost));
}

esp_err_t net_espnow_init(void) {
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    s_node_id = mac[5];

    esp_wifi_set_ps(WIFI_PS_NONE);   /* sin ahorro de energia: no pierde paquetes al escuchar */

    esp_err_t e = esp_now_init();
    if (e != ESP_OK) return e;
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BROADCAST_MAC, ESP_NOW_ETH_ALEN);
    peer.channel = 0;                /* 0 = canal actual del Wi-Fi */
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    e = esp_now_add_peer(&peer);
    if (e != ESP_OK) return e;

    s_ready = true;
    ESP_LOGI(TAG, "ESP-NOW listo. node_id=%02X", s_node_id);
    return ESP_OK;
}

esp_err_t net_espnow_send(const edge_reading_t *r) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    espnow_msg_t m = {
        .magic = MSG_MAGIC,
        .node_id = s_node_id,
        .seq = s_seq++,
        .temp_c = r->temp_c,
        .hum_rh = r->hum_rh,
        .alert = r->alert ? 1 : 0,
    };
    esp_err_t e = esp_now_send(BROADCAST_MAC, (const uint8_t *)&m, sizeof(m));
    if (e == ESP_OK) s_tx++;
    return e;
}