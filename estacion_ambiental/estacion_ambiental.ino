/*
  Estación Ambiental ESP32 — firmware para la API de la estación (FISI-UNAP)

  Mide:
    - MQ-135 (GPIO34, analógico): CO2, CO, NH4 y alcohol estimados en ppm
    - BMP280 (I2C 0x76/0x77):     temperatura (°C), presión (hPa), altitud (m)
    - RTC DS1307 (I2C):           fecha y hora local (Perú, UTC-05:00)
  Guarda cada lectura en la MicroSD (/datos.csv, CS en GPIO5) y la envía a la API
  con enviarLectura() cada INTERVALO_MS. Sincroniza el RTC por NTP al tener Wi-Fi.

  Archivos de esta carpeta:
    estacion_ambiental.ino  este programa
    api_estacion.h          envío HTTP(S) a la API
    secrets.h               TUS credenciales (copiar desde secrets.example.h; no subir a git)

  Librerías (Gestor de librerías del Arduino IDE):
    - Adafruit BMP280 Library (+ Adafruit Unified Sensor)
    - RTClib (Adafruit)
  Placa: "ESP32 Dev Module" (paquete esp32 de Espressif Systems). Monitor serie: 115200.

  Todo lo propio de cada estación (Wi-Fi, URL, token, calibración, divisor) va en secrets.h;
  este archivo no se necesita editar. Guía completa: instrucciones.md (raíz del repositorio).

  Primer uso: en secrets.h poner MODO_CALIBRACION 1, cargar, dejar el MQ-135 encendido en aire
  limpio al menos 20 minutos y copiar el R0 que aparece en el monitor serie a ESTACION_MQ_R0_KOHM.
  Luego volver MODO_CALIBRACION a 0 y cargar de nuevo.
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>
#include <Adafruit_BMP280.h>
#include <RTClib.h>

#include "secrets.h"
#include "api_estacion.h"

// ============================ CONFIGURACIÓN ============================

// Valores que se pueden definir en secrets.h (si no, se usan estos por defecto):
//   #define MODO_CALIBRACION           1      // solo calcula el R0 del MQ-135 en aire limpio
//   #define ESTACION_MQ_R0_KOHM        12.5   // R0 calibrado de TU sensor
//   #define ESTACION_MQ_DIVISOR        2.0    // factor del divisor de tensión del MQ-135
//   #define ESTACION_INTERVALO_MS      60000  // milisegundos entre lecturas
//   #define ESTACION_CALENTAMIENTO_MS  180000 // espera inicial del MQ-135
#ifndef MODO_CALIBRACION
#define MODO_CALIBRACION 0
#endif
#ifndef ESTACION_MQ_DIVISOR
#define ESTACION_MQ_DIVISOR 2.0
#endif
#ifndef ESTACION_INTERVALO_MS
#define ESTACION_INTERVALO_MS (60UL * 1000UL)
#endif
#ifndef ESTACION_CALENTAMIENTO_MS
#define ESTACION_CALENTAMIENTO_MS (3UL * 60UL * 1000UL)
#endif
#ifndef ESTACION_MQ_R0_KOHM
#define ESTACION_MQ_R0_KOHM 0.0   // <-- pon aquí el R0 calibrado (0 = gases sin enviar)
#endif

const uint32_t INTERVALO_MS     = ESTACION_INTERVALO_MS;     // una lectura por minuto
const uint32_t CALENTAMIENTO_MS = ESTACION_CALENTAMIENTO_MS; // el MQ-135 no es fiable al arrancar
const uint32_t RESYNC_NTP_MS    = 6UL * 3600UL * 1000UL; // re-sincronizar el RTC cada 6 h
const uint32_t REINTENTO_WIFI_MS = 30UL * 1000UL;

// Pines
const int PIN_MQ135 = 34;
const int PIN_SDA   = 21;
const int PIN_SCL   = 22;
const int PIN_SD_CS = 5;

// MQ-135
// El módulo se alimenta a 5 V y su salida puede llegar a 5 V, pero el ADC del ESP32 admite
// como máximo ~3,1 V. Usa un divisor de tensión (p. ej. dos resistencias iguales de 10 kΩ:
// ESTACION_MQ_DIVISOR = 2.0). Si conectas la salida directa (no recomendado), pon 1.0.
const float MQ_VCC                = 5.0;   // V, alimentación del módulo
const float MQ_DIVISOR            = ESTACION_MQ_DIVISOR;  // Vsalida_sensor = Vpin × MQ_DIVISOR
const float MQ_RL_KOHM            = 10.0;  // resistencia de carga del módulo (kΩ)
const float MQ_RATIO_AIRE_LIMPIO  = 3.6;   // Rs/R0 en aire limpio (hoja de datos)
const float MQ_R0_KOHM            = ESTACION_MQ_R0_KOHM;  // R0 calibrado (ver arriba)
const float CO2_BASE_PPM          = 400.0; // CO2 de fondo que la curva no incluye

// Curvas ppm = a × (Rs/R0)^b del MQ-135 (las mismas que usa la librería MQUnifiedsensor)
struct Curva { float a; float b; };
const Curva CURVA_CO2     = {110.47f, -2.862f};
const Curva CURVA_CO      = {605.18f, -3.937f};
const Curva CURVA_NH4     = {102.2f,  -2.473f};
const Curva CURVA_ALCOHOL = {77.255f, -3.18f};

// BMP280
const float PRESION_NIVEL_MAR_HPA = 1013.25;  // para estimar la altitud

// Hora
const long  OFFSET_UTC_S = -5L * 3600L;       // Perú, sin horario de verano
const char* NTP_1 = "pool.ntp.org";
const char* NTP_2 = "time.google.com";
const int   ANIO_MINIMO_VALIDO = 2025;        // si el RTC marca antes, la hora no es fiable

// =======================================================================

Adafruit_BMP280 bmp;
RTC_DS1307 rtc;

bool bmpOk = false;
bool rtcOk = false;
bool sdOk  = false;
bool ntpOk = false;

uint32_t ultimaLecturaMs = 0;
uint32_t ultimoNtpMs = 0;
uint32_t ultimoIntentoWifiMs = 0;
bool primeraLectura = true;

struct Medicion {
  float temperatura = NAN;
  float presion     = NAN;
  float altitud     = NAN;
  float co2         = NAN;
  float co          = NAN;
  float nh4         = NAN;
  float alcohol     = NAN;
};

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

// ------------------------------ MQ-135 ------------------------------

// Tensión real en la salida del módulo (V), promediando varias muestras.
float leerTensionMq() {
  uint32_t suma = 0;
  const int muestras = 20;
  for (int i = 0; i < muestras; i++) {
    suma += analogReadMilliVolts(PIN_MQ135);
    delay(5);
  }
  return (suma / (float) muestras) / 1000.0f * MQ_DIVISOR;
}

// Resistencia del sensor Rs (kΩ) a partir de la tensión de salida.
float calcularRs(float vSalida) {
  if (vSalida <= 0.01f || vSalida >= MQ_VCC) return NAN;
  return MQ_RL_KOHM * (MQ_VCC - vSalida) / vSalida;
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
    f.println("fecha_hora,temperatura_c,presion_hpa,altitud_m,co2_ppm,co_ppm,nh4_ppm,alcohol_ppm,enviado");
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
  f.print(","); f.print(numeroCsv(m.temperatura));
  f.print(","); f.print(numeroCsv(m.presion));
  f.print(","); f.print(numeroCsv(m.altitud, 1));
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

  if (bmpOk) {
    m.temperatura = enRango(bmp.readTemperature(), -10, 60);
    m.presion     = enRango(bmp.readPressure() / 100.0f, 300, 1100);
    m.altitud     = enRango(bmp.readAltitude(PRESION_NIVEL_MAR_HPA), -100, 5000);
  }

  const bool calentando = millis() < CALENTAMIENTO_MS;
  if (MQ_R0_KOHM > 0 && !calentando) {
    const float ratio = calcularRs(leerTensionMq()) / MQ_R0_KOHM;
    float co2 = ppmDesdeCurva(CURVA_CO2, ratio);
    if (!isnan(co2)) co2 += CO2_BASE_PPM;
    m.co2     = enRango(co2, 0, 10000);
    m.co      = enRango(ppmDesdeCurva(CURVA_CO, ratio), 0, 10000);
    m.nh4     = enRango(ppmDesdeCurva(CURVA_NH4, ratio), 0, 10000);
    m.alcohol = enRango(ppmDesdeCurva(CURVA_ALCOHOL, ratio), 0, 10000);
  }

  return m;
}

void imprimir(const DateTime& t, const Medicion& m) {
  Serial.println("--------------------------------------------------");
  Serial.printf("Hora: %s%s\n", fechaTexto(t).c_str(), horaValida(t) ? "" : " (NO VÁLIDA)");
  Serial.printf("Temperatura: %s °C | Presión: %s hPa | Altitud: %s m\n",
                numeroCsv(m.temperatura).c_str(), numeroCsv(m.presion).c_str(), numeroCsv(m.altitud, 1).c_str());
  if (MQ_R0_KOHM <= 0) {
    Serial.println("Gases: sin calibrar (ESTACION_MQ_R0_KOHM = 0). Usa MODO_CALIBRACION 1 en secrets.h.");
  } else if (millis() < CALENTAMIENTO_MS) {
    Serial.printf("Gases: calentando el MQ-135 (%lu s restantes)\n",
                  (unsigned long) ((CALENTAMIENTO_MS - millis()) / 1000));
  } else {
    Serial.printf("CO2: %s | CO: %s | NH4: %s | Alcohol: %s (ppm, estimados)\n",
                  numeroCsv(m.co2).c_str(), numeroCsv(m.co).c_str(),
                  numeroCsv(m.nh4).c_str(), numeroCsv(m.alcohol).c_str());
  }
}

// ------------------------------ Calibración ------------------------------

void bucleCalibracion() {
  Serial.println("\n=== MODO CALIBRACIÓN DEL MQ-135 ===");
  Serial.println("Deja el sensor en aire limpio (exterior o ambiente ventilado) al menos 20 minutos.");
  Serial.println("Copia a ESTACION_MQ_R0_KOHM el R0 promedio cuando se estabilice.\n");

  float suma = 0;
  uint32_t n = 0;
  while (true) {
    const float v = leerTensionMq();
    const float rs = calcularRs(v);
    if (!isnan(rs)) {
      const float r0 = rs / MQ_RATIO_AIRE_LIMPIO;
      suma += r0;
      n++;
      Serial.printf("Vsalida: %.3f V | Rs: %.2f kΩ | R0: %.2f kΩ | R0 promedio (%lu): %.2f kΩ\n",
                    v, rs, r0, (unsigned long) n, suma / n);
    } else {
      Serial.printf("Vsalida: %.3f V fuera de rango: revisa el cableado y ESTACION_MQ_DIVISOR.\n", v);
    }
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

  bmpOk = bmp.begin(0x76) || bmp.begin(0x77);
  if (bmpOk) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
  }
  Serial.printf("BMP280: %s\n", bmpOk ? "OK" : "NO DETECTADO (se enviará null)");

  rtcOk = rtc.begin();
  if (rtcOk && !rtc.isrunning()) {
    Serial.println("RTC detenido: se ajustará con NTP (o con la hora de compilación si no hay Wi-Fi).");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  Serial.printf("RTC DS1307: %s\n", rtcOk ? "OK" : "NO DETECTADO (se usará solo NTP)");

  sdOk = SD.begin(PIN_SD_CS);
  Serial.printf("MicroSD: %s\n", sdOk ? "OK" : "NO DETECTADA (sin respaldo local)");
  prepararCsv();

  if (MQ_R0_KOHM <= 0) {
    Serial.println("MQ-135: sin calibrar; los gases se enviarán como null.");
  }

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
      enviado = enviarLectura(t, m.temperatura, m.presion, m.altitud, m.co2, m.co, m.nh4, m.alcohol);
    }

    guardarCsv(t, m, enviado);
  }

  delay(50);
}
