// =============================================================================
//  secrets.example.h — PLANTILLA. No la edites aquí.
//
//  1. Copia este archivo en la MISMA carpeta con el nombre  secrets.h
//  2. Completa secrets.h con tus datos (ver instrucciones.md, paso 5).
//
//  secrets.h está en .gitignore: NUNCA lo subas a GitHub ni lo compartas.
// =============================================================================

#pragma once

// -----------------------------------------------------------------------------
// 1) TU RED WI-FI  (la completas tú)
//    - Debe ser de 2,4 GHz: el ESP32 no se conecta a redes de 5 GHz.
//    - Respeta mayúsculas, minúsculas y espacios del nombre.
// -----------------------------------------------------------------------------
#define WIFI_SSID     "NOMBRE_DE_TU_WIFI"
#define WIFI_PASSWORD "CONTRASENA_DE_TU_WIFI"

// -----------------------------------------------------------------------------
// 2) DATOS DE TU ESTACIÓN  (te los envía la coordinación por un canal interno)
//    - API_URL: dirección a la que se envían las lecturas. Termina en
//      /api/estaciones/<identificador-de-tu-estacion>/lecturas
//      Formato esperado (la dirección definitiva se confirma por interno):
//        "https://fisiunap.edu.pe/estacion-ambiental/api/estaciones/estacion-xx/lecturas"
//    - TOKEN_ESTACION: clave secreta de tu estación (40 caracteres).
//      Es personal: no la publiques, no la pongas en fotos ni capturas.
// -----------------------------------------------------------------------------
#define API_URL        "https://fisiunap.edu.pe/estacion-ambiental/api/estaciones/IDENTIFICADOR-DE-TU-ESTACION/lecturas"
#define TOKEN_ESTACION "PEGA_AQUI_EL_TOKEN_QUE_TE_ENVIARON"

// -----------------------------------------------------------------------------
// 3) CERTIFICADO RAÍZ DEL SERVIDOR  (también te lo envían por interno)
//    El ESP32 lo usa para comprobar que habla con el servidor real de la FISI (HTTPS).
//    Pega el bloque completo, línea por línea, respetando las comillas y los \n:
//
//    static const char ROOT_CA[] =
//      "-----BEGIN CERTIFICATE-----\n"
//      "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
//      "...\n"
//      "-----END CERTIFICATE-----\n";
// -----------------------------------------------------------------------------
static const char ROOT_CA[] = "";

// -----------------------------------------------------------------------------
// 4) CALIBRACIÓN DEL SENSOR MQ-135  (la haces tú, ver instrucciones.md, paso 7)
//    - Primera vez: deja MODO_CALIBRACION en 1, carga el programa y anota el
//      "R0 promedio" que aparece en el Monitor Serie después de ~20 minutos.
//    - Luego: pon MODO_CALIBRACION en 0 y escribe ese número en ESTACION_MQ_R0_KOHM.
//    Con ESTACION_MQ_R0_KOHM en 0 la estación funciona, pero envía los gases vacíos.
// -----------------------------------------------------------------------------
#define MODO_CALIBRACION     1
#define ESTACION_MQ_R0_KOHM  0.0

// -----------------------------------------------------------------------------
// 5) AJUSTES DE HARDWARE  (solo si tu montaje es distinto al de la guía)
// -----------------------------------------------------------------------------
// Divisor de tensión en la salida del MQ-135 (dos resistencias iguales = 2.0).
#define ESTACION_MQ_DIVISOR  2.0

// Opcionales (deja comentados para usar los valores por defecto):
// #define ESTACION_INTERVALO_MS      60000   // ms entre lecturas (por defecto, 1 minuto)
// #define ESTACION_CALENTAMIENTO_MS  180000  // ms de espera inicial del MQ-135 (3 minutos)
