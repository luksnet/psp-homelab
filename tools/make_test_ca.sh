#!/usr/bin/env bash
# Tworzy testowe CA i certyfikat serwera dla "localhost" (do atrapy Home Assistant).
# Użycie: tools/make_test_ca.sh <katalog_wyjściowy>
# Nie używaj tych kluczy poza testami.
set -euo pipefail
out="${1:?podaj katalog wyjściowy}"
mkdir -p "$out"
cd "$out"

# CA (EC P-256), ważne 30 dni
openssl ecparam -name prime256v1 -genkey -noout -out ca.key
openssl req -x509 -new -key ca.key -sha256 -days 30 \
    -subj "/CN=psp-homelab test CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -out ca.pem

# Certyfikat serwera dla localhost
openssl ecparam -name prime256v1 -genkey -noout -out server.key
openssl req -new -key server.key -subj "/CN=localhost" -out server.csr
printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\nextendedKeyUsage=serverAuth\nbasicConstraints=CA:FALSE\n' > server.ext
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
    -sha256 -days 30 -extfile server.ext -out server.pem
rm -f server.csr server.ext ca.srl

echo "Gotowe w $(pwd): ca.pem (dla PSP), server.pem + server.key (dla atrapy)"
