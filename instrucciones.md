# Instrucciones: conectar tu estación ambiental ESP32

Guía paso a paso para armar, programar y conectar tu estación a la plataforma de la FISI-UNAP. No necesitas experiencia previa: sigue los pasos en orden.

**Qué vas a lograr:** tu ESP32 medirá el aire cada minuto, guardará cada lectura en una tarjeta MicroSD y la enviará por Wi-Fi a la API de la FISI, donde aparecerá en un dashboard en tiempo real.

```
 MQ-135 (gases) ─┐
 BMP280 (clima) ─┼──► ESP32 ──Wi-Fi──► API FISI (fisiunap.edu.pe) ──► Dashboard
 DS1307 (reloj) ─┤        │
 MicroSD ────────┘        └──► /datos.csv (respaldo en la tarjeta)
```

**Tiempo estimado:** 1 a 2 horas la primera vez (la mayor parte es instalar programas), más 20 minutos de calibración.

---

## Índice

1. [Antes de empezar: lo que te enviará la coordinación](#1-antes-de-empezar-lo-que-te-enviará-la-coordinación)
2. [Materiales](#2-materiales)
3. [Conexiones](#3-conexiones)
4. [Instalar el Arduino IDE y el soporte para ESP32](#4-instalar-el-arduino-ide-y-el-soporte-para-esp32)
5. [Descargar el programa y crear tu `secrets.h`](#5-descargar-el-programa-y-crear-tu-secretsh)
6. [Cargar el programa en la placa](#6-cargar-el-programa-en-la-placa)
7. [Calibrar el sensor de gases (MQ-135)](#7-calibrar-el-sensor-de-gases-mq-135)
8. [Poner la estación en marcha y comprobar que envía](#8-poner-la-estación-en-marcha-y-comprobar-que-envía)
9. [Instalación física](#9-instalación-física)
10. [Solución de problemas](#10-solución-de-problemas)
11. [Seguridad y buenas prácticas](#11-seguridad-y-buenas-prácticas)
12. [Preguntas frecuentes](#12-preguntas-frecuentes)

---

## 1. Antes de empezar: lo que te enviará la coordinación

Cada estación tiene sus propias credenciales. La coordinación del proyecto te las enviará **por un canal interno** (nunca por un grupo público):

| Dato | Ejemplo | Para qué sirve |
|---|---|---|
| **Identificador de la estación** | `estacion-07` | Nombre único de tu estación en la plataforma |
| **URL de la API** | `https://fisiunap.edu.pe/estacion-ambiental/api/estaciones/estacion-07/lecturas` | A dónde envía las lecturas tu ESP32 |
| **Token** | 40 letras y números | Tu "contraseña" ante la API. **Es secreto** |
| **Certificado raíz (`ROOT_CA`)** | Bloque de texto que empieza con `-----BEGIN CERTIFICATE-----` | Permite al ESP32 comprobar que se conecta al servidor real de la FISI |
| **Dirección del dashboard** | `https://fisiunap.edu.pe/estacion-ambiental/estaciones/estacion-07` | Donde verás tus mediciones |

> Las direcciones exactas se confirmarán cuando la plataforma se publique en **fisiunap.edu.pe**. Usa siempre las que te envíe la coordinación.

Además necesitas el **nombre y la contraseña de una red Wi-Fi de 2,4 GHz** en el lugar donde funcionará la estación.

---

## 2. Materiales

| Cantidad | Componente | Notas |
|---|---|---|
| 1 | **ESP32 DevKit** (ESP32-WROOM-32, 30 o 38 pines) | Opcional: placa de expansión para facilitar las conexiones |
| 1 | **Módulo MQ-135** (sensor de calidad del aire) | Tiene 4 pines: VCC, GND, DO y AO |
| 1 | **Módulo BMP280** (temperatura y presión) | Versión I2C (pines VCC, GND, SCL y SDA) |
| 1 | **Módulo RTC DS1307** con su pila CR2032 | Reloj; la pila mantiene la hora si se corta la luz |
| 1 | **Módulo lector de MicroSD** (SPI) | Más una **MicroSD de 32 GB o menos**, formateada en **FAT32** |
| 2 | **Resistencias de 10 kΩ** (marrón, negro, naranja) | Para el divisor de tensión del MQ-135 (**obligatorio**) |
| 1 | Protoboard y cables jumper | |
| 1 | **Cable USB de datos** | Los cables "solo carga" no sirven para programar |
| 1 | **Fuente de 5 V y 2 A** (cargador de celular con USB) | ⚠️ Una **pila de 9 V no sirve**: el sensor MQ-135 se la acaba en pocas horas |

> **Si tu caja trae dos sensores MQ:** por ahora el programa solo lee el **MQ-135** en el pin GPIO34. Identifica cuál es por el número impreso en su costado. El segundo sensor puede quedar conectado a la alimentación, pero no se mide.

---

## 3. Conexiones

> ⚠️ **Haz todas las conexiones con la placa desconectada del USB y de la fuente.**

### 3.1 Tabla de conexiones

| Módulo | Pin del módulo | Va al ESP32 | Notas |
|---|---|---|---|
| **MQ-135** | VCC | **VIN (5 V)** | El calentador del sensor necesita 5 V |
| | GND | GND | |
| | AO (salida analógica) | **GPIO34**, **a través del divisor de tensión** (ver 3.2) | ⚠️ Nunca directo |
| | DO | Sin conectar | No se usa |
| **BMP280** | VCC | **3V3** | ⚠️ No lo conectes a 5 V |
| | GND | GND | |
| | SDA | **GPIO21** | Bus I2C, compartido con el RTC |
| | SCL | **GPIO22** | Bus I2C, compartido con el RTC |
| **RTC DS1307** | VCC | **VIN (5 V)** | El DS1307 necesita 5 V |
| | GND | GND | |
| | SDA | **GPIO21** | Mismo cable o fila que el SDA del BMP280 |
| | SCL | **GPIO22** | Mismo cable o fila que el SCL del BMP280 |
| **MicroSD** | VCC | **VIN (5 V)** si el módulo tiene regulador (un chip de 3 patas, p. ej. AMS1117); si no, **3V3** | Revisa tu módulo |
| | GND | GND | |
| | CS | **GPIO5** | |
| | MOSI | **GPIO23** | |
| | MISO | **GPIO19** | |
| | SCK | **GPIO18** | |

**Todos los GND deben estar unidos entre sí** (usa la línea azul o negra de la protoboard).

### 3.2 Divisor de tensión del MQ-135 (obligatorio)

El MQ-135 entrega hasta **5 V** por su pin AO, pero el ESP32 solo soporta **3,3 V** en sus entradas. Si lo conectas directo puedes **dañar el ESP32**. Por eso se usan dos resistencias iguales de 10 kΩ, que dejan pasar la mitad de la tensión:

```
  MQ-135 AO ──[ 10 kΩ ]──┬──► GPIO34 del ESP32
                         │
                      [ 10 kΩ ]
                         │
                        GND
```

- **Con dos resistencias iguales:** no tienes que cambiar nada (`ESTACION_MQ_DIVISOR 2.0`, el valor por defecto).
- **Con otras resistencias:** el factor es `(R1 + R2) / R2`, donde R1 es la que va al AO y R2 la que va a GND. Escribe ese número en `ESTACION_MQ_DIVISOR` dentro de tu `secrets.h`.

### 3.3 Revisión antes de energizar

- [ ] Ningún cable de 5 V toca un pin de 3V3 ni un GPIO.
- [ ] El BMP280 está en **3V3**, no en 5 V.
- [ ] El AO del MQ-135 pasa por las **dos resistencias** antes de llegar a GPIO34.
- [ ] Todos los GND están unidos.
- [ ] La MicroSD está insertada y formateada en FAT32.
- [ ] El RTC tiene su pila.

---

## 4. Instalar el Arduino IDE y el soporte para ESP32

### 4.1 Arduino IDE

1. Descarga el **Arduino IDE 2** desde https://www.arduino.cc/en/software e instálalo.
2. Ábrelo una vez y ciérralo (así crea sus carpetas).

### 4.2 Soporte para ESP32

1. Abre el Arduino IDE y ve a **Archivo → Preferencias**.
2. En **"URLs adicionales del Gestor de Placas"** pega:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
   y pulsa **Aceptar**.
3. Ve a **Herramientas → Placa → Gestor de placas**, busca **esp32** y en **"esp32 by Espressif Systems"** pulsa **Instalar**. Probado con la versión **3.3.12**.

> ⏳ **Ten paciencia:** son unos **2 GB** de descarga. Si se corta (error de descarga), vuelve a pulsar **Instalar**: continúa desde donde quedó.

### 4.3 Librerías

Ve a **Herramientas → Gestionar bibliotecas** (o **Sketch → Incluir biblioteca → Gestionar bibliotecas**) e instala:

| Busca | Instala |
|---|---|
| `Adafruit BMP280` | **Adafruit BMP280 Library**. Si pregunta por dependencias, elige **Instalar todas**: se instalan *Adafruit Unified Sensor* y *Adafruit BusIO* |
| `RTClib` | **RTClib** (de Adafruit) |

> No instales ninguna librería llamada "SD", "WiFi" ni "HTTPClient": ya vienen con el paquete ESP32, y una copia aparte puede causar errores.

---

## 5. Descargar el programa y crear tu `secrets.h`

### 5.1 Descargar

1. En la página de este repositorio en GitHub, pulsa **Code → Download ZIP**.
2. Descomprime el ZIP en una carpeta fácil de encontrar, por ejemplo `Documentos\Arduino\`.
3. Entra a la carpeta **`estacion_ambiental`**. Deben estar estos archivos:
   ```
   estacion_ambiental/
   ├── estacion_ambiental.ino
   ├── api_estacion.h
   └── secrets.example.h
   ```
   > La carpeta **debe llamarse `estacion_ambiental`**, igual que el archivo `.ino`. Si le cambias el nombre, el Arduino IDE no la abre.

### 5.2 Crear `secrets.h`

`secrets.h` es el archivo donde va **toda la configuración de tu estación**. No hace falta editar ningún otro archivo.

1. En el Explorador de Windows activa **Vista → Mostrar → Extensiones de nombre de archivo**, para ver el `.h` al final de los nombres.
2. **Copia** `secrets.example.h` y **pega** la copia en la misma carpeta.
3. **Renombra** la copia a exactamente **`secrets.h`**. No debe quedar `secrets.h.txt` ni `secrets - copia.h`.
4. Abre `estacion_ambiental.ino` con el Arduino IDE: aparecerán las pestañas `api_estacion.h`, `secrets.example.h` y `secrets.h`.
5. En la pestaña **`secrets.h`** completa los datos entre comillas:

```cpp
// 1) Tu Wi-Fi (2,4 GHz)
#define WIFI_SSID     "MiRedWiFi"
#define WIFI_PASSWORD "miContraseña123"

// 2) Lo que te envió la coordinación
#define API_URL        "https://fisiunap.edu.pe/estacion-ambiental/api/estaciones/estacion-07/lecturas"
#define TOKEN_ESTACION "aQ3...(los 40 caracteres que te enviaron)..."

// 3) Certificado raíz que te enviaron (cada línea entre comillas y terminada en \n)
static const char ROOT_CA[] =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIF...\n"
  "...\n"
  "-----END CERTIFICATE-----\n";

// 4) Calibración: la primera vez, MODO_CALIBRACION en 1 (ver paso 7)
#define MODO_CALIBRACION     1
#define ESTACION_MQ_R0_KOHM  0.0
```

6. Guarda con **Ctrl+S**.

> ⚠️ **No pongas tu `secrets.h` en GitHub, en grupos ni en capturas de pantalla.** Si alguien obtiene tu token, puede enviar datos falsos a nombre de tu estación.

---

## 6. Cargar el programa en la placa

1. Conecta el ESP32 a la computadora con el **cable USB de datos**.
2. En **Herramientas → Placa → esp32** elige **ESP32 Dev Module**.
3. En **Herramientas → Puerto** elige el **COM** que apareció al conectar la placa.
   - Si no aparece ningún COM nuevo, falta el driver USB. Mira el chip junto al conector USB de tu placa:
     - **CP2102:** instala el driver *CP210x* de Silicon Labs (https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers).
     - **CH340:** instala el driver *CH340* de WCH (https://www.wch-ic.com/downloads/CH341SER_EXE.html).
   - Desconecta y vuelve a conectar la placa después de instalar el driver.
4. Abre **Herramientas → Monitor Serie** y elige **115200 baudios** abajo a la derecha.
5. Pulsa **Subir** (la flecha →). La primera compilación tarda varios minutos.
   - Si aparece `Connecting........_____` y luego un error, **mantén presionado el botón BOOT** de la placa mientras dice `Connecting...` y suéltalo cuando empiece a escribir porcentajes.
6. Al terminar dirá **"Hard resetting via RTS pin..."**: el programa ya está en la placa.

---

## 7. Calibrar el sensor de gases (MQ-135)

Cada sensor MQ-135 es distinto. Para que las estimaciones tengan sentido hay que medir su **R0**, su resistencia en aire limpio. Se hace **una sola vez por sensor**.

> 💡 **Si tu MQ-135 es nuevo**, déjalo energizado al menos **24 horas** antes de calibrar (el "quemado" inicial que indica el fabricante). Si ya lo usaste antes, basta con 20 minutos.

1. Verifica que en `secrets.h` esté `#define MODO_CALIBRACION 1` y carga el programa (paso 6).
2. Lleva la estación a un lugar con **aire limpio y ventilado** (cerca de una ventana abierta o al aire libre), **lejos** de cocinas, humo, vehículos, alcohol o productos de limpieza.
3. Mira el Monitor Serie. Aparecerá cada 2 segundos algo como:
   ```
   === MODO CALIBRACIÓN DEL MQ-135 ===
   Vsalida: 1.842 V | Rs: 17.14 kΩ | R0: 4.76 kΩ | R0 promedio (25): 4.81 kΩ
   ```
4. Espera **al menos 20 minutos**, hasta que el **R0 promedio** casi no cambie.
5. **Anota el R0 promedio** (en el ejemplo, `4.81`).
6. En `secrets.h` cambia:
   ```cpp
   #define MODO_CALIBRACION     0
   #define ESTACION_MQ_R0_KOHM  4.81   // ← tu valor
   ```
7. Guarda y **carga el programa de nuevo**.

> Si ves `Vsalida: ... fuera de rango`, revisa el divisor de tensión y la conexión del AO a GPIO34.

---

## 8. Poner la estación en marcha y comprobar que envía

Con `MODO_CALIBRACION 0`, al encender la placa el Monitor Serie debe mostrar algo así:

```
Estación Ambiental ESP32
BMP280: OK
RTC DS1307: OK
MicroSD: OK
[WiFi] Conectando a MiRedWiFi.....
[WiFi] Conectado. IP: 192.168.1.37
[NTP] Hora sincronizada: 2026-11-10 09:15:02
--------------------------------------------------
Hora: 2026-11-10 09:15:02
Temperatura: 27.84 °C | Presión: 1008.12 hPa | Altitud: 43.2 m
Gases: calentando el MQ-135 (178 s restantes)
[API] Lectura enviada (201).
```

- **Los primeros 3 minutos** los gases van vacíos mientras el sensor se calienta. Es normal.
- **Cada minuto** debe aparecer una lectura nueva con **`[API] Lectura enviada (201)`**.
- **En el dashboard** (la dirección que te envió la coordinación), tu estación debe aparecer **"En línea"** y mostrar tus valores.

**Cuando funcione en la computadora**, desconéctala y alimenta la estación con la **fuente de 5 V y 2 A** por el USB de la placa. Funciona sola: no necesita la computadora.

---

## 9. Instalación física

- **Bajo techo o en una caja** con aberturas, protegida de **lluvia** y **sol directo**: el sol eleva la temperatura medida y el agua daña los módulos.
- **Que circule el aire** alrededor del MQ-135: **no lo tapes**, no lo selles con silicona ni lo encierres en una caja sin ventilación.
- **Separa el BMP280 del MQ-135:** el calentador del MQ-135 se pone tibio y altera la temperatura medida. Unos 5 a 10 cm de distancia ayudan.
- **Lejos de fuentes que alteren las mediciones**, si no son lo que quieres medir: cocinas, extractores, estufas o escapes.
- **Con buena señal de Wi-Fi:** si la señal es débil, verás reconexiones frecuentes en el Monitor Serie.
- **Fuente siempre conectada:** sin energía no hay mediciones. El RTC conserva la hora gracias a su pila.

---

## 10. Solución de problemas

| Lo que ves en el Monitor Serie | Causa probable | Qué hacer |
|---|---|---|
| No aparece nada | Velocidad del monitor incorrecta | Pon **115200 baudios**; pulsa el botón **EN/RST** de la placa |
| Caracteres raros (`⸮⸮⸮`) | Velocidad incorrecta | **115200 baudios** |
| Error al compilar: `secrets.h: No such file or directory` | Falta `secrets.h` o quedó mal nombrado | Paso 5.2: debe llamarse exactamente `secrets.h` y estar junto al `.ino` |
| Error al compilar sobre `ROOT_CA` | Certificado mal pegado | Cada línea entre comillas y terminada en `\n"`, y un `;` al final |
| `[WiFi] Sin conexión (¿red de 2,4 GHz? ¿contraseña?)` | Red de 5 GHz, contraseña mal escrita o señal débil | Usa una red de **2,4 GHz**; revisa mayúsculas y minúsculas; acércala al router |
| `BMP280: NO DETECTADO` | Cableado I2C o alimentación | SDA → 21, SCL → 22, VCC → **3V3**. La estación sigue funcionando sin temperatura ni presión |
| `RTC DS1307: NO DETECTADO` | Cableado I2C o alimentación | SDA → 21, SCL → 22, VCC → 5 V. Funciona igual con la hora de internet |
| `MicroSD: NO DETECTADA` | Tarjeta no insertada, formato o cableado | Formatea en **FAT32** (32 GB o menos); revisa CS → 5, MOSI → 23, MISO → 19, SCK → 18 |
| `Hora: ... (NO VÁLIDA)` y no envía | Sin hora: RTC sin pila y sin internet | Revisa el Wi-Fi; pon pila al RTC |
| `Gases: sin calibrar` | `ESTACION_MQ_R0_KOHM` en 0 | Haz la calibración (paso 7) |
| Gases siempre vacíos o `fuera de rango` | Divisor de tensión mal armado o AO desconectado | Revisa el paso 3.2 |
| `[API] Error 401: Token inválido` | Token mal copiado, o estación desactivada | Copia el token **completo**, sin espacios. Si sigue, avisa a la coordinación |
| `[API] Error 404` | Identificador de la estación mal escrito en `API_URL` | Revisa la URL que te enviaron |
| `[API] Error 422` | Hora incorrecta, más de 5 minutos adelantada | Revisa el Wi-Fi (sincroniza la hora). Si persiste, avisa con una captura **sin** `secrets.h` |
| `[API] Error 429` | Demasiados envíos o intentos | Espera un minuto. No reduzcas `ESTACION_INTERVALO_MS` por debajo de 60000 |
| `[API] Falta ROOT_CA en secrets.h para usar HTTPS.` | `ROOT_CA` vacío | Pega el certificado que te enviaron |
| `[API] Fallo de conexión: ...` | Sin internet, URL mal escrita o certificado incorrecto | Comprueba que la red tenga internet; revisa `API_URL` y `ROOT_CA` |
| La placa se reinicia sola, o `Brownout detector was triggered` | Alimentación insuficiente | Usa la fuente de **5 V y 2 A**, no una pila de 9 V ni un USB débil |
| Al subir: `Failed to connect to ESP32` | La placa no entró en modo de carga | Mantén presionado **BOOT** mientras dice `Connecting...` (paso 6) |

---

## 11. Seguridad y buenas prácticas

- **El token es como una contraseña.** No lo compartas, no lo subas a internet y no lo muestres en fotos ni videos.
- **Si tu token se expone** (por ejemplo, subiste `secrets.h` por error), avisa de inmediato a la coordinación: generarán uno nuevo y el anterior dejará de funcionar.
- **No modifiques `API_URL`** para enviar datos a otras estaciones. Cada token solo sirve para su propia estación.
- **No elimines** la validación del certificado (`ROOT_CA`): protege tus datos en el camino hacia el servidor.
- **Los datos de gases son estimaciones** del MQ-135. Para afirmar que el aire está contaminado, compáralos con fuentes oficiales.

---

## 12. Preguntas frecuentes

**¿Qué pasa si se va el internet?**
La estación sigue midiendo y guarda todo en la MicroSD (`/datos.csv`), con la columna `enviado` en `0`. Reintenta conectarse al Wi-Fi cada 30 segundos.

**¿Qué pasa si se va la luz?**
Al volver la energía, la estación arranca sola. El RTC mantiene la hora gracias a su pila, y al conectarse a internet la vuelve a sincronizar.

**¿Cómo cambio la red Wi-Fi?**
Edita `WIFI_SSID` y `WIFI_PASSWORD` en `secrets.h` y vuelve a cargar el programa.

**¿Tengo que calibrar cada vez?**
No. Solo una vez por sensor MQ-135. Si cambias el sensor, recalibra.

**¿Cómo veo los datos guardados en la MicroSD?**
Apaga la estación, saca la tarjeta y abre `datos.csv` con Excel o Google Sheets.

**¿Puedo medir con más frecuencia?**
No conviene: la plataforma acepta como máximo 10 envíos por minuto por estación, y una lectura por minuto es suficiente para el monitoreo ambiental.

**¿A quién le pregunto?**
A la coordinación del proyecto de responsabilidad social de la FISI-UNAP. Al consultar, indica el **identificador de tu estación** y adjunta lo que muestra el Monitor Serie, **sin incluir tu `secrets.h`**.
