"""Local-only TLS integration: temporary CA, HTTPS server and native ABI client."""
import http.server
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
import time

class Handler(http.server.BaseHTTPRequestHandler):
    def do_PUT(self):
        count = int(self.headers.get('Content-Length', '0'))
        received = bytearray()
        while len(received) < count:
            chunk = self.rfile.read(min(4096, count - len(received)))
            if not chunk:
                break
            received.extend(chunk)
            time.sleep(0.001)  # Exercise encrypted socket backpressure.
        expected = bytes(range(256)) * 4096
        self.send_response(201 if received == expected else 400)
        self.send_header('Content-Length', '0')
        self.end_headers()
        try:
            self.connection.unwrap()
        except (ssl.SSLError, OSError):
            pass

    def do_GET(self):
        body = b'native-tls-ok'
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        try:
            self.connection.unwrap()  # Send close_notify, not an unclean TCP EOF.
        except (ssl.SSLError, OSError):
            pass

    def log_message(self, *args):
        pass

with tempfile.TemporaryDirectory(prefix='nxnative-tls-') as folder:
    root = Path(folder)
    config = root / 'openssl.cnf'
    config.write_text('[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n'
                      '[dn]\nCN=localhost\n[ext]\nsubjectAltName=DNS:localhost\n'
                      'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,digitalSignature,keyEncipherment\n'
                      'extendedKeyUsage=serverAuth\n')
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256', '-days', '1',
                    '-config', str(config), '-keyout', str(root / 'key.pem'), '-out', str(root / 'cert.pem')],
                   check=True, capture_output=True)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(root / 'cert.pem', root / 'key.pem')
    context.set_alpn_protocols(['http/1.1'])
    subprocess.run(['openssl', 'x509', '-in', str(root / 'cert.pem'), '-outform', 'DER', '-out', str(root / 'cert.der')], check=True)
    with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
        server.socket = context.wrap_socket(server.socket, server_side=True)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            result = subprocess.run([str(Path(sys.argv[1]).resolve()), str(server.server_port), str(root / 'cert.pem'), str(root / 'cert.der')], timeout=40)
        finally:
            server.shutdown()
    sys.exit(result.returncode)
