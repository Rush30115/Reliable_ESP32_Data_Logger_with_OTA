import http.server
import ssl
import os

PORT = 8070
CERT_FILE = os.path.expanduser("~/esp/reliable_data_logger/main/certs/server_cert.pem")
KEY_FILE = os.path.expanduser("~/server_key.pem")

if not os.path.exists(CERT_FILE):
    CERT_FILE = "server_cert.pem"
if not os.path.exists(KEY_FILE):
    KEY_FILE = "server_key.pem"

handler = http.server.SimpleHTTPRequestHandler
server = http.server.HTTPServer(('0.0.0.0', PORT), handler)

context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(certfile=CERT_FILE, keyfile=KEY_FILE)
server.socket = context.wrap_socket(server.socket, server_side=True)

print(f"🔒 Serving HTTPS on https://127.0.1.1:{PORT}/ (Port {PORT})")
print("📁 Serving files from current directory. Press Ctrl+C to stop.")
try:
    server.serve_forever()
except KeyboardInterrupt:
    print("\nServer stopped.")
