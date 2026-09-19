#!/usr/bin/env python3
"""Exercise the actual AppImage download/swap path against a local HTTPS server.

On macOS this compiles the POSIX AppImage branch as a host executable; it does
not claim to test Linux AppImage runtime launching. No production signing key,
update URL, game profile, installed game, or public release is used.
"""
import argparse
import base64
import datetime
import functools
import hashlib
import http.server
import ipaddress
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ed25519, rsa
from cryptography.x509.oid import NameOID

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    key = ed25519.Ed25519PrivateKey.generate()
    public = base64.b64encode(key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)).decode()
    with tempfile.TemporaryDirectory(prefix="appimage-flow-") as temp:
        root = Path(temp)
        tls_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
        now = datetime.datetime.now(datetime.timezone.utc)
        cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(tls_key.public_key())
                .serial_number(x509.random_serial_number()).not_valid_before(now-datetime.timedelta(minutes=1))
                .not_valid_after(now+datetime.timedelta(hours=1))
                .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
                .add_extension(x509.SubjectAlternativeName([x509.DNSName("localhost"), x509.IPAddress(ipaddress.ip_address("127.0.0.1"))]), critical=False)
                .sign(tls_key, hashes.SHA256()))
        ca = root/"ca.pem"; ca.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
        private = root/"tls.pem"; private.write_bytes(tls_key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())); private.chmod(0o600)
        class Handler(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *unused): pass
        server = http.server.ThreadingHTTPServer(("127.0.0.1",0), functools.partial(Handler,directory=str(root)))
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.load_cert_chain(ca, private)
        server.socket = context.wrap_socket(server.socket,server_side=True)
        thread = threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        base = f"https://localhost:{server.server_port}"
        (out/"UpdateConfig.h").write_text(f'#define DUNECITY_UPDATE_FEED_BASE "{base}"\n#define DUNECITY_UPDATE_PUBLIC_KEY "{public}"\n#define DUNECITY_UPDATE_PLATFORM "linux-x86_64"\n')
        source = (ROOT/"src/misc/DesktopUpdater.cpp").read_text()
        # Only this disposable diagnostic source trusts our temporary CA. The
        # shipped updater has no environment-based trust override.
        marker = '    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);'
        if source.count(marker)!=1: raise RuntimeError("HTTPS test injection point changed")
        source=source.replace(marker,marker+'\n    curl_easy_setopt(curl.get(), CURLOPT_CAINFO, getenv("UPDATE_TEST_CA"));')
        source = source.replace("defined(__linux__)", "(defined(__linux__) || defined(DUNECITY_TEST_POSIX))")
        (out/"DesktopUpdater.cpp").write_text(source)
        (out/"main.cpp").write_text('''#include <misc/DesktopUpdater.h>
#include <chrono>
#include <thread>
#include <iostream>
namespace NativeUpdater { void cleanup() {} }
int main() {
    auto& u = DesktopUpdater::instance();
    if (!u.supported()) return 4;
    u.check();
    for (int i=0; i<1500; ++i) {
        u.poll();
        if (u.state()==DesktopUpdater::State::Available) u.install();
        if (u.state()==DesktopUpdater::State::Current) return 3;
        if (u.state()==DesktopUpdater::State::Restart) return 0;
        if (u.state()==DesktopUpdater::State::Failed) { std::cerr<<u.message(); return 1; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 2;
}
''')
        flags = subprocess.check_output(["pkg-config","--cflags","--libs","sdl2","libcurl","openssl"],text=True).split()
        binary = out/"appimage-flow"
        command = ["c++","-std=c++17","-DDUNECITY_DESKTOP_UPDATER=1","-DDUNECITY_TEST_POSIX=1","-I"+str(out),"-I"+str(ROOT/"include"),
                   str(out/"main.cpp"),str(out/"DesktopUpdater.cpp"),str(ROOT/"src/Network/UpdateManifest.cpp"),str(ROOT/"src/misc/AppImageUpdate.cpp"),"-o",str(binary)]+flags
        subprocess.run(command,check=True)
        image = b"\x7fELF"+bytes([2,1,1,0])+b"AI\x02"+b"verified new game"*4096
        (root/"update.AppImage").write_bytes(image)
        old = b"previous game must survive failed updates"
        cases = ["success","corrupt","truncated","tampered","downgrade","wrong-platform","untrusted-tls"]
        with (out/"results.log").open("w") as log:
            for case in cases:
                game = root/(case+".AppImage"); game.write_bytes(old)
                version = "0.0.1" if case=="downgrade" else "9.0.0"
                platform = "windows-x64" if case=="wrong-platform" else "linux-x86_64"
                sha = "0"*64 if case=="corrupt" else hashlib.sha256(image).hexdigest()
                size = len(image)+(1 if case=="truncated" else 0)
                payload=f"DuneCityUpdate1\n{version}\n{platform}\n{base}/update.AppImage\n{size}\n{sha}\n".encode()
                manifest=payload+base64.b64encode(key.sign(payload))+b"\n"
                if case=="tampered":manifest=manifest.replace(b"9.0.0",b"9.0.1")
                (root/"updates-linux-x86_64.txt").write_bytes(manifest)
                env=dict(os.environ,APPIMAGE=str(game),UPDATE_TEST_CA=str(root/"missing.pem" if case=="untrusted-tls" else ca))
                result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=20)
                expected=0 if case=="success" else 3 if case=="downgrade" else 1
                if result.returncode!=expected: raise RuntimeError(f"{case}: exit {result.returncode}: {result.stderr}")
                if game.read_bytes()!=(image if case=="success" else old): raise RuntimeError(case+": wrong installed content")
                if list(root.glob(case+".AppImage.download-*")):raise RuntimeError(case+": partial download left behind")
                if case=="success":
                    backups=list(root.glob(case+".AppImage.previous-*"))
                    if len(backups)!=1 or backups[0].read_bytes()!=old:raise RuntimeError("Previous app backup missing")
                print("PASS",case);print("PASS",case,file=log)
        server.shutdown();server.server_close()


if __name__=="__main__":main()
