#!/usr/bin/env bash
# Instala Mosquitto + Node-RED en la Raspberry, importa el flujo y sirve la interfaz.
# Correr SIN sudo, desde cualquier carpeta:  bash scripts/instalar-pi.sh
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "$(id -u)" -eq 0 ]; then
  echo "Correr como usuario normal, sin sudo:  bash scripts/instalar-pi.sh"
  exit 1
fi

echo "== Mosquitto =="
sudo apt-get update
sudo apt-get install -y mosquitto mosquitto-clients curl
sudo cp mosquitto/delta.conf /etc/mosquitto/conf.d/delta.conf
sudo systemctl enable mosquitto
sudo systemctl restart mosquitto
sleep 2
if ! systemctl is-active --quiet mosquitto; then
  sudo journalctl -u mosquitto -n 30 --no-pager
  echo "Mosquitto no arranco, revisar el log de arriba"
  exit 1
fi
ss -ltn | grep ':1883 ' >/dev/null || { echo "Mosquitto no escucha en 1883"; exit 1; }
ss -ltn | grep ':9001 ' >/dev/null || { echo "Mosquitto no escucha en 9001 (websockets)"; exit 1; }

echo "== Node-RED =="
if ! command -v node-red >/dev/null 2>&1; then
  bash <(curl -sL https://raw.githubusercontent.com/node-red/linux-installers/master/deb/update-nodejs-and-nodered) --confirm-install --confirm-pi
fi
sudo systemctl enable nodered.service
sudo systemctl restart nodered.service

SETTINGS="$HOME/.node-red/settings.js"
for i in $(seq 1 90); do [ -f "$SETTINGS" ] && break; sleep 2; done
[ -f "$SETTINGS" ] || { echo "No se genero $SETTINGS. Ver: journalctl -u nodered"; exit 1; }

echo "== Interfaz web =="
mkdir -p "$HOME/delta-web"
cp web/* "$HOME/delta-web/"
if grep -qE "^[[:space:]]*httpStatic:" "$SETTINGS"; then
  echo "httpStatic ya estaba configurado en $SETTINGS (revisar que apunte a $HOME/delta-web)"
else
  sed -i "s|^[[:space:]]*//[[:space:]]*httpStatic:.*|    httpStatic: '$HOME/delta-web',|" "$SETTINGS"
  grep -qE "^[[:space:]]*httpStatic:" "$SETTINGS" || { echo "No pude configurar httpStatic: agregalo a mano en $SETTINGS"; exit 1; }
fi
sudo systemctl restart nodered.service

for i in $(seq 1 90); do curl -sf http://localhost:1880/flows >/dev/null && break; sleep 2; done
curl -sf http://localhost:1880/flows >/dev/null || { echo "Node-RED no responde en :1880. Ver: journalctl -u nodered"; exit 1; }

echo "== Flujo =="
if [ "$(curl -s http://localhost:1880/flows | tr -d ' \n')" = "[]" ]; then
  curl -sf -X POST http://localhost:1880/flows -H 'Content-Type: application/json' --data @node-red/flujo-delta.json >/dev/null
  echo "Flujo importado"
else
  echo "Node-RED ya tiene flujos: importar node-red/flujo-delta.json a mano"
fi

mosquitto_pub -h localhost -t delta/test -m ok
IP=$(hostname -I | awk '{print $1}')
echo
echo "Listo."
echo "Interfaz: http://$IP:1880/proyecto-delta.html   (host = $IP, puerto 9001)"
echo "Node-RED: http://$IP:1880"
echo "Dejar IP fija a esta Raspberry en el router."
