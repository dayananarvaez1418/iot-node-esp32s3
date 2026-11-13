/**
 * @file net_edge.h
 * @brief Envio de lecturas del nodo al servidor edge por HTTPS (POST).
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/** @brief Lectura que el nodo reporta al servidor edge. */
typedef struct {
    char  ts[24];   /**< Fecha y hora del DS3231 ("AAAA-MM-DD HH:MM:SS") */
    float temp_c;   /**< Temperatura en grados C */
    float hum_rh;   /**< Humedad relativa en % */
    float z;        /**< Z-score calculado por la IA local */
    bool  alert;    /**< true si se detecto una anomalia */
} edge_reading_t;

/**
 * @brief Crea la cola y la tarea de envio. Llamar despues de conectar a Wi-Fi.
 * @return ESP_OK si todo se creo correctamente
 */
esp_err_t net_edge_init(void);

/**
 * @brief Encola una lectura para enviarla al servidor (no bloquea).
 * @param r Lectura a enviar
 * @return true si se encolo; false si el modulo no esta listo o la cola esta llena
 */
bool net_edge_publish(const edge_reading_t *r);