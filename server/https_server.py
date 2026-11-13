"""Servidor HTTPS del nodo edge.

Hace dos cosas:
  1. GET  /firmware_v2.bin  -> entrega el firmware OTA desde server/firmware.
  2. POST /api/lectura      -> recibe lecturas JSON del nodo y las guarda en
                               server/lecturas.csv.

Uso:
    python server/https_server.py
"""
import argparse
import csv
import functools
import http.server
import json
import os
import ssl
import threading
from datetime import datetime

CSV_PATH = "server/lecturas.csv"
CSV_HEADER = ["recibido", "ts_nodo", "temp_c", "hum_rh", "z", "alerta", "rtt_ms"]
MAX_BODY = 1024            # bytes: evita cuerpos enormes
csv_lock = threading.Lock()


def guardar_lectura(d):
    """Agrega una fila al CSV (crea el archivo con cabecera si no existe)."""
    nuevo = not os.path.exists(CSV_PATH)
    with csv_lock, open(CSV_PATH, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if nuevo:
            w.writerow(CSV_HEADER)
        w.writerow([
            datetime.now().isoformat(timespec="seconds"),
            d.get("ts", ""),
            d.get("t", ""),
            d.get("h", ""),
            d.get("z", ""),
            d.get("alert", ""),
            d.get("rtt_ms", ""),
        ])


class Handler(http.server.SimpleHTTPRequestHandler):
    """GET entrega archivos; POST /api/lectura recibe lecturas del nodo."""

    def do_POST(self):
        if self.path != "/api/lectura":
            self.send_error(404, "Ruta no encontrada")
            return

        largo = int(self.headers.get("Content-Length", 0))
        if largo <= 0 or largo > MAX_BODY:
            self.send_error(400, "Cuerpo vacio o demasiado grande")
            return

        try:
            datos = json.loads(self.rfile.read(largo))
            float(datos["t"])
            float(datos["h"])
        except (ValueError, KeyError, TypeError):
            self.send_error(400, "JSON invalido")
            return

        guardar_lectura(datos)
        print(f"[lectura] {datos}")

        respuesta = b'{"ok":true}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(respuesta)))
        self.end_headers()
        self.wfile.write(respuesta)


def main():
    parser = argparse.ArgumentParser(description="Servidor HTTPS del nodo edge")
    parser.add_argument("--port", type=int, default=8070)
    parser.add_argument("--cert", default="main/server_certs/ca_cert.pem")
    parser.add_argument("--key", default="server/server_key.pem")
    parser.add_argument("--dir", default="server/firmware")
    args = parser.parse_args()

    handler = functools.partial(Handler, directory=args.dir)
    httpd = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), handler)

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=args.cert, keyfile=args.key)
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)

    print(f"Sirviendo '{args.dir}' y /api/lectura en https://0.0.0.0:{args.port}")
    httpd.serve_forever()


if __name__ == "__main__":
    main()