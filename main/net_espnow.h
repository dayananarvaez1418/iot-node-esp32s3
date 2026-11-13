/**
 * @file net_espnow.h
 * @brief Comunicacion nodo-nodo por ESP-NOW (broadcast, sin router).
 */
#pragma once

#include "esp_err.h"
#include "net_edge.h"

/**
 * @brief Inicia ESP-NOW. Llamar despues de conectar a Wi-Fi (el Wi-Fi debe estar activo).
 * @return ESP_OK si se inicio correctamente
 */
esp_err_t net_espnow_init(void);

/**
 * @brief Transmite una lectura en broadcast a los nodos vecinos.
 * @param r Lectura a transmitir
 * @return ESP_OK si se envio; ESP_ERR_INVALID_STATE si el modulo no esta listo
 */
esp_err_t net_espnow_send(const edge_reading_t *r);