# Estación Ambiental ESP32 — FISI-UNAP

Firmware para estaciones de monitoreo ambiental con **ESP32**, desarrollado para la actividad de responsabilidad social de la Facultad de Ingeniería de Sistemas e Informática (FISI) de la Universidad Nacional de la Amazonía Peruana.

La estación mide, cada minuto:
- **calidad del aire** con un **MQ-135**: CO₂, CO, amoníaco y alcohol, como estimaciones;
- **temperatura, presión y altitud** con un **BMP280**.

Cada lectura se guarda en una **MicroSD**, con fecha y hora de un reloj **DS1307** sincronizado por internet, y se envía por Wi-Fi a la **API de la FISI**, donde se publica en un dashboard en tiempo real.

> 👉 **¿Tienes el hardware y quieres conectar tu estación?** Sigue la guía paso a paso: **[instrucciones.md](instrucciones.md)**.

## Contenido

| Ruta | Qué es |
|---|---|
| [`instrucciones.md`](instrucciones.md) | Guía completa: materiales, conexiones, instalación, configuración, calibración y solución de problemas |
| [`estacion_ambiental/`](estacion_ambiental/) | Carpeta del programa (ábrela con el Arduino IDE) |
| `estacion_ambiental/estacion_ambiental.ino` | Programa principal |
| `estacion_ambiental/api_estacion.h` | Envío de lecturas a la API (HTTPS) |
| `estacion_ambiental/secrets.example.h` | Plantilla de configuración. Se copia como `secrets.h`, que **nunca** se sube |

## Requisitos rápidos

- **Placa:** ESP32 DevKit (ESP32-WROOM-32), en el Arduino IDE como **ESP32 Dev Module**.
- **Arduino IDE 2.x** con el paquete de placas **esp32** de Espressif (probado con la versión 3.3.12).
- **Librerías:** Adafruit BMP280 Library, Adafruit Unified Sensor, Adafruit BusIO y RTClib.
- **Credenciales de tu estación** (identificador, token y certificado), que entrega la coordinación por un canal interno.

## Seguridad

- **`secrets.h` es personal y está en `.gitignore`.** Contiene tu Wi-Fi y el token de tu estación. Si se expone por accidente, avisa a la coordinación para que **rote el token**.
- **No subas** capturas del Monitor Serie ni fotos que muestren el contenido de `secrets.h`.

## Aviso sobre los datos

Las concentraciones de gases del MQ-135 son **estimaciones** que dependen de la calibración del sensor. No sustituyen un instrumento certificado.
