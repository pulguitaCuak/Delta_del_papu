# Proyecto Delta

Interfaz para programar secuencias (x, y, z + agarrar/soltar) de varios robots delta (hasta 99, pensado para ~20 en un sector)
y mandarlas por MQTT a los ESP32. Raspberry Pi 5 con Mosquitto y Node-RED; cada ESP32 recibe la secuencia, responde como se va a mover e informa su estado.

## Estructura
```
web/proyecto-delta.html        interfaz (un solo archivo)
web/mqtt.min.js                libreria mqtt.js (para que ande sin internet en la LAN)
mosquitto/delta.conf           broker (1883 mqtt, 9001 websockets)
node-red/flujo-delta.json      flujo (escucha delta/#)
esp32/delta_esp32/             sketch Arduino (copiar secrets.h.example a secrets.h)
scripts/instalar-pi.sh         instala todo en la Raspberry
```

## Instalar en la Raspberry
Raspberry Pi OS (Bookworm, Lite sirve) con internet para instalar. Sin sudo:
```bash
git clone <URL_DEL_REPO>
cd proyecto-delta
bash scripts/instalar-pi.sh
```
El script instala Mosquitto y Node-RED, copia la interfaz, importa el flujo y al final imprime las URLs.
Interfaz: `http://IP_DE_LA_PI:1880/proyecto-delta.html` (el host ya viene cargado, puerto 9001).
Dejar IP fija a la Pi en el router.

## Varios deltas
- En la interfaz: grilla de deltas 1..N (campo `deltas`). Cada delta tiene su propia secuencia.
- Colores: gris = sin conexion, normal = online, verde = moviendose, borde azul = seleccionado.
- `Enviar a N` manda solo al delta elegido; `Enviar a todos` copia la secuencia a todos y la manda a `delta/todos/secuencia`.
- Cada ESP32 se carga con su propio `ID` en `secrets.h` (1, 2, 3 ... 20).
- Las secuencias y la config se guardan en el navegador (localStorage).

## Topics
| Topic | Sentido | Payload |
|---|---|---|
| `delta/<id>/secuencia` | interfaz -> ESP | `{"puntos":[{"x":0,"y":0,"z":100,"a":"mover"}],"lazo":false}` |
| `delta/todos/secuencia` | interfaz -> todos los ESP | igual |
| `delta/<id>/plan` | ESP -> interfaz | misma estructura (como se va a mover) |
| `delta/<id>/estado` | ESP -> interfaz | `{"punto":3,"estado":"moviendo"}` (`punto` 0 = sin punto) |
| `delta/<id>/online` | ESP -> interfaz | `1` al conectar, `0` por LWT si se cae (retain) |

`a` puede ser `mover`, `agarrar` o `soltar`. Rangos: x/y de -150 a 150 mm, z de 0 a 200 mm.
Si un ESP esta ejecutando una secuencia y le llega otra, responde estado `ocupado` y la ignora.

## Probar
```bash
mosquitto_sub -h localhost -t 'delta/#' -v
mosquitto_pub -h localhost -t delta/1/online -r -m 1
mosquitto_pub -h localhost -t delta/1/estado -m '{"punto":1,"estado":"moviendo"}'
```

## Notas
- El ESP32 usa la IP de la Pi, no `raspberrypi.local`.
- La interfaz no conecta a `ws://` si se abre desde https; abrirla desde Node-RED o como archivo local.
- La secuencia no se manda con retain, para que un ESP no ejecute una vieja al reiniciar.
- Broker sin usuario ni password: solo para red local.
