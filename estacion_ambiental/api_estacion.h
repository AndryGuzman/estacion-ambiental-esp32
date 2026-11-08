// api_estacion.h — Envío de lecturas del ESP32 a la API de la Estación Ambiental.
//
// Uso (en el sketch, después de incluir WiFi.h, HTTPClient.h y RTClib.h):
//   #include "secrets.h"        // copia de secrets.example.h con tus datos (NO subir a git)
//   #include "api_estacion.h"
//   ...
//   bool ok = enviarLectura(rtc.now(), temp, presion, altitud, co2, co, nh4, alcohol);
//
// - Las medidas que no se pudieron leer se pasan como NAN y se envían como null.
// - La hora del RTC debe estar en hora de Perú (UTC-05:00).
// - Con API_URL https:// se valida el certificado del servidor con ROOT_CA.
//   Con http:// (solo para pruebas en la red local) se usa una conexión sin cifrar.
// - Devuelve true si la API respondió 201 (creada) o 200 (ya existía).

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <RTClib.h>
#include <math.h>

#ifndef API_ZONA_HORARIA
#define API_ZONA_HORARIA "-05:00"
#endif

#ifndef API_TIMEOUT_MS
#define API_TIMEOUT_MS 8000
#endif

// Agrega "clave":valor al JSON; NAN se convierte en null.
static void apiAgregarNumero(String& json, const char* clave, float valor, uint8_t decimales = 2) {
  json += ",\"";
  json += clave;
  json += "\":";
  if (isnan(valor) || isinf(valor)) {
    json += "null";
  } else {
    json += String(valor, (unsigned int) decimales);
  }
}

static String apiConstruirJson(const DateTime& t, float temp, float pres, float alt,
                               float co2, float co, float nh4, float alc) {
  char fecha[32];
  snprintf(fecha, sizeof(fecha), "%04d-%02d-%02dT%02d:%02d:%02d%s",
           t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second(), API_ZONA_HORARIA);

  String json;
  json.reserve(256);
  json += "{\"medido_en\":\"";
  json += fecha;
  json += "\"";
  apiAgregarNumero(json, "temperatura_c", temp);
  apiAgregarNumero(json, "presion_hpa", pres);
  apiAgregarNumero(json, "altitud_m", alt, 1);
  apiAgregarNumero(json, "co2_ppm", co2);
  apiAgregarNumero(json, "co_ppm", co);
  apiAgregarNumero(json, "nh4_ppm", nh4);
  apiAgregarNumero(json, "alcohol_ppm", alc);
  json += "}";
  return json;
}

bool enviarLectura(const DateTime& t, float temp, float pres, float alt,
                   float co2, float co, float nh4, float alc) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[API] Sin Wi-Fi: lectura solo en la MicroSD.");
    return false;
  }

  const String url = API_URL;
  const bool seguro = url.startsWith("https://");

  WiFiClientSecure clienteSeguro;
  WiFiClient clientePlano;
  HTTPClient http;
  http.setTimeout(API_TIMEOUT_MS);

  bool iniciado;
  if (seguro) {
#if defined(API_INSEGURO_PRUEBAS) && API_INSEGURO_PRUEBAS
    // SOLO PARA PRUEBAS (simulador Wokwi + túnel): no valida el certificado del servidor.
    // Nunca activar en la placa real.
    clienteSeguro.setInsecure();
#else
    if (strlen(ROOT_CA) == 0) {
      Serial.println("[API] Falta ROOT_CA en secrets.h para usar HTTPS.");
      return false;
    }
    clienteSeguro.setCACert(ROOT_CA);
#endif
    iniciado = http.begin(clienteSeguro, url);
  } else {
    iniciado = http.begin(clientePlano, url);
  }

  if (!iniciado) {
    Serial.println("[API] No se pudo iniciar la conexión.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("Authorization", String("Bearer ") + TOKEN_ESTACION);

  const String cuerpo = apiConstruirJson(t, temp, pres, alt, co2, co, nh4, alc);
  const int codigo = http.POST(cuerpo);

  if (codigo == 200 || codigo == 201) {
    Serial.printf("[API] Lectura enviada (%d).\n", codigo);
  } else if (codigo > 0) {
    Serial.printf("[API] Error %d: %s\n", codigo, http.getString().c_str());
  } else {
    Serial.printf("[API] Fallo de conexión: %s\n", http.errorToString(codigo).c_str());
  }

  http.end();
  return codigo == 200 || codigo == 201;
}
