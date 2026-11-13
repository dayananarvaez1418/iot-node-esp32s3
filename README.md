## Estado del proyecto (avance)

| Requisito | Estado |
|---|---|
| 1. OTA | Implementado: Wi-Fi, descarga por HTTPS con verificación de certificado, rollback y disparo con botón BOOT (pulsación larga). Compila. Pendiente probar en placa. |
| 2. IA en el ESP32 | Implementado: z-score en ventana móvil + tasa de cambio sobre el SHT3x, con hora del DS3231. Compila. Pendiente probar con el sensor. |
| 3. Interfaz HMI | Parcial: botón BOOT (pulsación larga). La tarjeta no tiene IMU, por lo que no hay gestos con acelerómetro. |
| 4. Red nodo-edge / nodo-nodo | En desarrollo. |

### Notas
- Servidor HTTPS en Python (`server/https_server.py`) probado con `curl`.
- Pines I2C (SDA=8, SCL=9) y dirección del SHT3x (0x44) pendientes de confirmar con el esquemático.
- Para compilar: copiar `main/wifi_secrets.h.example` a `main/wifi_secrets.h` y poner los datos de la red.
- Seguridad: la clave privada y la contraseña de Wi-Fi no se suben al repositorio.