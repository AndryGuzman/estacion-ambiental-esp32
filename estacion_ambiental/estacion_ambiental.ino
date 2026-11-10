/*
  Estación Ambiental ESP32 — firmware para la API de la estación (FISI-UNAP)

  Mide:
    - MQ-135 (GPIO34, analógico): CO2, NH4 y alcohol estimados en ppm
    - MQ-7   (GPIO35, analógico): CO (monóxido de carbono) estimado en ppm
    - RTC DS1307 (I2C):           fecha y hora local (Perú, UTC-05:00)
  Guarda cada lectura en la MicroSD (/datos.csv, CS en GPIO5) y la envía a la API
  con enviarLectura() cada INTERVALO_MS. Sincroniza el RTC por NTP al tener Wi-Fi.

  Archivos de esta carpeta:
    estacion_ambiental.ino  este programa
    api_estacion.h          envío HTTP(S) a la API
    secrets.h               TUS credenciales (copiar desde secrets.example.h; no subir a git)

  Librerías (Gestor de librerías del Arduino IDE):
    - RTClib (Adafruit)
  Placa: "ESP32 Dev Module" (paquete esp32 de Espressif Systems). Monitor serie: 115200.

  Todo lo propio de cada estación (Wi-Fi, URL, token, calibración, divisor) va en secrets.h;
  este archivo no se necesita editar. Guía completa: instrucciones.md (raíz del repositorio).

  Primer uso: en secrets.h poner MODO_CALIBRACION 1, cargar, dejar los dos sensores encendidos en
  aire limpio al menos 20 minutos y copiar los R0 que aparecen en el monitor serie a
  ESTACION_MQ135_R0_KOHM y ESTACION_MQ7_R0_KOHM. Luego volver MODO_CALIBRACION a 0 y cargar de nuevo.
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>
#include <RTClib.h>

#include "secrets.h"
#include "api_estacion.h"

// ============================ CONFIGURACIÓN ============================

// Valores que se pueden definir en secrets.h (si no, se usan estos por defecto):
//   #define MODO_CALIBRACION           1      // solo calcula el R0 de los dos sensores en aire limpio
//   #define ESTACION_MQ135_R0_KOHM     12.5   // R0 calibrado de TU MQ-135
//   #define ESTACION_MQ7_R0_KOHM       1.8    // R0 calibrado de TU MQ-7
//   #define ESTACION_MQ135_DIVISOR     2.0    // factor del divisor de tensión del MQ-135
//   #define ESTACION_MQ7_DIVISOR       2.0    // factor del divisor de tensión del MQ-7
//   #define ESTACION_INTERVALO_MS      60000  // milisegundos entre lecturas
//   #define ESTACION_CALENTAMIENTO_MS  180000 // espera inicial de los sensores
#ifndef MODO_CALIBRACION
#define MODO_CALIBRACION 0
#endif
#ifndef ESTACION_MQ135_DIVISOR
#define ESTACION_MQ135_DIVISOR 2.0
#endif
#ifndef ESTACION_MQ7_DIVISOR
#define ESTACION_MQ7_DIVISOR 2.0
#endif
#ifndef ESTACION_INTERVALO_MS
#define ESTACION_INTERVALO_MS (60UL * 1000UL)
#endif
#ifndef ESTACION_CALENTAMIENTO_MS
#define ESTACION_CALENTAMIENTO_MS (3UL * 60UL * 1000UL)
#endif
#ifndef ESTACION_MQ135_R0_KOHM
#define ESTACION_MQ135_R0_KOHM 0.0   // 0 = sin calibrar: CO2, NH4 y alcohol se envían vacíos
#endif
#ifndef ESTACION_MQ7_R0_KOHM
#define ESTACION_MQ7_R0_KOHM 0.0     // 0 = sin calibrar: el CO se envía vacío
#endif

const uint32_t INTERVALO_MS     = ESTACION_INTERVALO_MS;     // una lectura por minuto
const uint32_t CALENTAMIENTO_MS = ESTACION_CALENTAMIENTO_MS; // los MQ no son fiables al arrancar
const uint32_t RESYNC_NTP_MS    = 6UL * 3600UL * 1000UL; // re-sincronizar el RTC cada 6 h
const uint32_t REINTENTO_WIFI_MS = 30UL * 1000UL;

// Pines (GPIO34 y GPIO35 son del ADC1, el único que funciona con el Wi-Fi encendido)
const int PIN_MQ135 = 34;
const int PIN_MQ7   = 35;
const int PIN_SDA   = 21;
const int PIN_SCL   = 22;
const int PIN_SD_CS = 5;

// Sensores MQ
// Los módulos se alimentan a 5 V y su salida puede llegar a 5 V, pero el ADC del ESP32 admite
// como máximo ~3,1 V. Usa un divisor de tensión en cada salida (p. ej. dos resistencias iguales
// de 10 kΩ: divisor = 2.0). Si conectas la salida directa (no recomendado), pon 1.0.
const float MQ_VCC     = 5.0;   // V, alimentación de los módulos
const float MQ_RL_KOHM = 10.0;  // resistencia de carga de los módulos (kΩ)

struct SensorMq {
  const char* nombre;
  int   pin;
  float divisor;            // Vsalida_sensor = Vpin × divisor
  float ratioAireLimpio;    // Rs/R0 en aire limpio (hoja de datos)
  float r0;                 // R0 calibrado (kΩ); 0 = sin calibrar
};

const SensorMq MQ135 = {"MQ-135", PIN_MQ135, ESTACION_MQ135_DIVISOR, 3.6f,  ESTACION_MQ135_R0_KOHM};
const SensorMq MQ7   = {"MQ-7",   PIN_MQ7,   ESTACION_MQ7_DIVISOR,   27.5f, ESTACION_MQ7_R0_KOHM};

const float CO2_BASE_PPM = 400.0; // CO2 de fondo que la curva del MQ-135 no incluye

// Curvas ppm = a × (Rs/R0)^b (las mismas que usa la librería MQUnifiedsensor)
struct Curva { float a; float b; };
const Curva CURVA_CO2     = {110.47f, -2.862f};  // MQ-135
const Curva CURVA_NH4     = {102.2f,  -2.473f};  // MQ-135
const Curva CURVA_ALCOHOL = {77.255f, -3.18f};   // MQ-135
const Curva CURVA_CO_MQ7  = {99.042f, -1.518f};  // MQ-7

// Hora
const long  OFFSET_UTC_S = -5L * 3600L;       // Perú, sin horario de verano
const char* NTP_1 = "pool.ntp.org";
const char* NTP_2 = "time.google.com";
const int   ANIO_MINIMO_VALIDO = 2025;        // si el RTC marca antes, la hora no es fiable

// =======================================================================

RTC_DS1307 rtc;

bool rtcOk = false;
bool sdOk  = false;
bool ntpOk = false;

uint32_t ultimaLecturaMs = 0;
uint32_t ultimoNtpMs = 0;
uint32_t ultimoIntentoWifiMs = 0;
bool primeraLectura = true;

struct Medicion {
  float co2         = NAN;
  float co          = NAN;
  float nh4         = NAN;
  float alcohol     = NAN;
};

// Promedio acumulado del R0 durante la calibración
struct Acumulado { float suma = 0; uint32_t n = 0; };

// ---------------------------- utilidades ----------------------------

String numeroCsv(float v, uint8_t decimales = 2) {
  return (isnan(v) || isinf(v)) ? String("") : String(v, (unsigned int) decimales);
}

String fechaTexto(const DateTime& t) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
  return String(buf);
}

// ------------------------------ Wi-Fi ------------------------------

bool conectarWifi(uint32_t esperaMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[WiFi] WIFI_SSID vacío en secrets.h");
    return false;
  }

  Serial.printf("[WiFi] Conectando a %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < esperaMs) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("[WiFi] Sin conexión (¿red de 2,4 GHz? ¿contraseña?). Se reintentará.");
  return false;
}

// ------------------------------ Hora -------------------------------

// Ajusta el RTC con la hora de internet (hora local de Perú).
bool sincronizarHora() {
  if (WiFi.status() != WL_CONNECTED) return false;

  configTime(OFFSET_UTC_S, 0, NTP_1, NTP_2);
  struct tm t;
  if (!getLocalTime(&t, 10000)) {
    Serial.println("[NTP] No se obtuvo la hora.");
    return false;
  }

  if (rtcOk) {
    rtc.adjust(DateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec));
  }
  Serial.printf("[NTP] Hora sincronizada: %04d-%02d-%02d %02d:%02d:%02d\n",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  return true;
}

// Hora actual: del RTC si existe; si no, del reloj interno sincronizado por NTP.
DateTime ahora() {
  if (rtcOk) return rtc.now();

  struct tm t;
  if (ntpOk && getLocalTime(&t, 100)) {
    return DateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  }
  return DateTime((uint32_t) 0);
}

bool horaValida(const DateTime& t) {
  return t.year() >= ANIO_MINIMO_VALIDO && t.year() < 2100;
}

// ---------------------------- Sensores MQ ----------------------------

// Tensión real en la salida del módulo (V), promediando varias muestras.
float leerTensionMq(const SensorMq& sensor) {
  uint32_t suma = 0;
  const int muestras = 20;
  for (int i = 0; i < muestras; i++) {
    suma += analogReadMilliVolts(sensor.pin);
    delay(5);
  }
  return (suma / (float) muestras) / 1000.0f * sensor.divisor;
}

// Resistencia del sensor Rs (kΩ) a partir de la tensión de salida.
float calcularRs(float vSalida) {
  if (vSalida <= 0.01f || vSalida >= MQ_VCC) return NAN;
  return MQ_RL_KOHM * (MQ_VCC - vSalida) / vSalida;
}

// Rs/R0 actual del sensor; NAN si no está calibrado o la lectura no es válida.
float ratioActual(const SensorMq& sensor) {
  if (sensor.r0 <= 0) return NAN;
  return calcularRs(leerTensionMq(sensor)) / sensor.r0;
}

float ppmDesdeCurva(const Curva& c, float ratio) {
  if (isnan(ratio) || ratio <= 0) return NAN;
  return c.a * powf(ratio, c.b);
}

// ------------------------------ MicroSD ------------------------------

void prepararCsv() {
  if (!sdOk || SD.exists("/datos.csv")) return;
  File f = SD.open("/datos.csv", FILE_WRITE);
  if (f) {
    f.println("fecha_hora,co2_ppm,co_ppm,nh4_ppm,alcohol_ppm,enviado");
    f.close();
  }
}

void guardarCsv(const DateTime& t, const Medicion& m, bool enviado) {
  if (!sdOk) return;
  File f = SD.open("/datos.csv", FILE_APPEND);
  if (!f) {
    Serial.println("[SD] No se pudo abrir /datos.csv");
    return;
  }
  f.print(horaValida(t) ? fechaTexto(t) : String(""));
  f.print(","); f.print(numeroCsv(m.co2));
  f.print(","); f.print(numeroCsv(m.co));
  f.print(","); f.print(numeroCsv(m.nh4));
  f.print(","); f.print(numeroCsv(m.alcohol));
  f.print(","); f.println(enviado ? "1" : "0");
  f.close();
}

// ------------------------------ Medición ------------------------------

// La API rechaza la lectura ENTERA (422) si un valor sale de su rango; por eso un valor
// imposible se envía como null y el resto de la lectura se conserva.
float enRango(float v, float minimo, float maximo) {
  if (isnan(v) || isinf(v) || v < minimo || v > maximo) return NAN;
  return v;
}

Medicion medir() {
  Medicion m;
  if (millis() < CALENTAMIENTO_MS) return m;

  const float ratio135 = ratioActual(MQ135);
  float co2 = ppmDesdeCurva(CURVA_CO2, ratio135);
  if (!isnan(co2)) co2 += CO2_BASE_PPM;
  m.co2     = enRango(co2, 0, 10000);
  m.nh4     = enRango(ppmDesdeCurva(CURVA_NH4, ratio135), 0, 10000);
  m.alcohol = enRango(ppmDesdeCurva(CURVA_ALCOHOL, ratio135), 0, 10000);

  m.co = enRango(ppmDesdeCurva(CURVA_CO_MQ7, ratioActual(MQ7)), 0, 10000);
  return m;
}

void imprimir(const DateTime& t, const Medicion& m) {
  Serial.println("--------------------------------------------------");
  Serial.printf("Hora: %s%s\n", fechaTexto(t).c_str(), horaValida(t) ? "" : " (NO VÁLIDA)");
  if (millis() < CALENTAMIENTO_MS) {
    Serial.printf("Gases: calentando los sensores (%lu s restantes)\n",
                  (unsigned long) ((CALENTAMIENTO_MS - millis()) / 1000));
    return;
  }
  if (MQ135.r0 <= 0) {
    Serial.println("MQ-135: sin calibrar (ESTACION_MQ135_R0_KOHM = 0). Usa MODO_CALIBRACION 1 en secrets.h.");
  } else {
    Serial.printf("MQ-135 -> CO2: %s | NH4: %s | Alcohol: %s (ppm, estimados)\n",
                  numeroCsv(m.co2).c_str(), numeroCsv(m.nh4).c_str(), numeroCsv(m.alcohol).c_str());
  }
  if (MQ7.r0 <= 0) {
    Serial.println("MQ-7: sin calibrar (ESTACION_MQ7_R0_KOHM = 0). Usa MODO_CALIBRACION 1 en secrets.h.");
  } else {
    Serial.printf("MQ-7   -> CO: %s (ppm, estimado)\n", numeroCsv(m.co).c_str());
  }
}

// ------------------------------ Calibración ------------------------------

void calibrarUno(const SensorMq& sensor, Acumulado& acc) {
  const float v = leerTensionMq(sensor);
  const float rs = calcularRs(v);
  if (isnan(rs)) {
    Serial.printf("%-6s Vsalida: %.3f V fuera de rango: revisa el cableado y el divisor.\n", sensor.nombre, v);
    return;
  }
  const float r0 = rs / sensor.ratioAireLimpio;
  acc.suma += r0;
  acc.n++;
  Serial.printf("%-6s Vsalida: %.3f V | Rs: %.2f kΩ | R0: %.2f kΩ | R0 promedio (%lu): %.2f kΩ\n",
                sensor.nombre, v, rs, r0, (unsigned long) acc.n, acc.suma / acc.n);
}

void bucleCalibracion() {
  Serial.println("\n=== MODO CALIBRACIÓN DEL MQ-135 Y DEL MQ-7 ===");
  Serial.println("Deja los sensores en aire limpio (exterior o ambiente ventilado) al menos 20 minutos.");
  Serial.println("Cuando se estabilicen, copia los R0 promedio a ESTACION_MQ135_R0_KOHM y ESTACION_MQ7_R0_KOHM.\n");

  Acumulado acc135, acc7;
  while (true) {
    calibrarUno(MQ135, acc135);
    calibrarUno(MQ7, acc7);
    Serial.println();
    delay(2000);
  }
}

// ------------------------------ setup / loop ------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nEstación Ambiental ESP32");

  analogReadResolution(12);

  if (MODO_CALIBRACION) {
    bucleCalibracion();  // no retorna
  }

  Wire.begin(PIN_SDA, PIN_SCL);

  rtcOk = rtc.begin();
  if (rtcOk && !rtc.isrunning()) {
    Serial.println("RTC detenido: se ajustará con NTP (o con la hora de compilación si no hay Wi-Fi).");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  Serial.printf("RTC DS1307: %s\n", rtcOk ? "OK" : "NO DETECTADO (se usará solo NTP)");

  sdOk = SD.begin(PIN_SD_CS);
  Serial.printf("MicroSD: %s\n", sdOk ? "OK" : "NO DETECTADA (sin respaldo local)");
  prepararCsv();

  if (MQ135.r0 <= 0) Serial.println("MQ-135: sin calibrar; CO2, NH4 y alcohol se enviarán como null.");
  if (MQ7.r0 <= 0)   Serial.println("MQ-7: sin calibrar; el CO se enviará como null.");

  if (conectarWifi(15000)) {
    ntpOk = sincronizarHora();
    ultimoNtpMs = millis();
  }
  ultimoIntentoWifiMs = millis();
}

void loop() {
  // Reconexión Wi-Fi sin bloquear las mediciones por mucho tiempo
  if (WiFi.status() != WL_CONNECTED && millis() - ultimoIntentoWifiMs >= REINTENTO_WIFI_MS) {
    ultimoIntentoWifiMs = millis();
    conectarWifi(8000);
  }

  // Sincronización NTP inicial (si no se logró) y periódica
  if (WiFi.status() == WL_CONNECTED && (!ntpOk || millis() - ultimoNtpMs >= RESYNC_NTP_MS)) {
    ultimoNtpMs = millis();
    if (sincronizarHora()) ntpOk = true;
  }

  if (primeraLectura || millis() - ultimaLecturaMs >= INTERVALO_MS) {
    primeraLectura = false;
    ultimaLecturaMs = millis();

    const DateTime t = ahora();
    const Medicion m = medir();
    imprimir(t, m);

    bool enviado = false;
    if (!horaValida(t)) {
      Serial.println("[API] Hora no válida: no se envía (revisa el RTC o el Wi-Fi/NTP).");
    } else {
      enviado = enviarLectura(t, m.co2, m.co, m.nh4, m.alcohol);
    }

    guardarCsv(t, m, enviado);
  }

  delay(50);
}
