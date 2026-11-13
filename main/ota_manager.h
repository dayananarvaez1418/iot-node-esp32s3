/**
 * @file ota_manager.h
 * @brief Wi-Fi y actualizacion OTA por HTTPS con rollback.
 */
#pragma once

#include "esp_err.h"

/** @brief Conecta a Wi-Fi como estacion y espera la IP. */
esp_err_t wifi_connect(void);

/** @brief Marca la imagen actual como valida (cancela el rollback). */
void ota_mark_valid(void);

/** @brief Descarga el firmware desde OTA_URL, lo instala y reinicia. */
esp_err_t ota_perform_update(void);

/** @brief Escribe en el log la version del firmware en ejecucion. */
void ota_log_version(void);