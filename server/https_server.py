"""Servidor HTTPS minimo para entregar firmware OTA al ESP32.

Uso:
    python server/https_server.py
"""
import argparse
import functools
import http.server
import ssl


def main():
    parser = argparse.ArgumentParser(description="Servidor HTTPS de firmware OTA")
    parser.add_argument("--port", type=int, default=8070)
    parser.add_argument("--cert", default="main/server_certs/ca_cert.pem")
    parser.add_argument("--key", default="server/server_key.pem")
    parser.add_argument("--dir", default="server/firmware")
    args = parser.parse_args()

    handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                                directory=args.dir)
    httpd = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), handler)

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=args.cert, keyfile=args.key)
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)

    print(f"Sirviendo '{args.dir}' en https://0.0.0.0:{args.port}")
    httpd.serve_forever()


if __name__ == "__main__":
    main()