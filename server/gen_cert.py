"""Genera certificado autofirmado y clave para el servidor OTA.

Uso:
    python server/gen_cert.py                 -> cubre 172.20.10.1-14 (hotspot iPhone)
    python server/gen_cert.py 192.168.1.21    -> solo esa(s) IP(s)
"""
import datetime
import ipaddress
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

ips = sys.argv[1:] or [f"172.20.10.{i}" for i in range(1, 15)]

key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "ota-server")])
now = datetime.datetime.now(datetime.timezone.utc)
san = [x509.IPAddress(ipaddress.ip_address(ip)) for ip in ips]

cert = (x509.CertificateBuilder()
        .subject_name(name).issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=365))
        .add_extension(x509.SubjectAlternativeName(san), critical=False)
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .sign(key, hashes.SHA256()))

with open("server/server_key.pem", "wb") as f:
    f.write(key.private_bytes(serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL,
            serialization.NoEncryption()))
with open("main/server_certs/ca_cert.pem", "wb") as f:
    f.write(cert.public_bytes(serialization.Encoding.PEM))
print("Certificado generado para:", ", ".join(ips))