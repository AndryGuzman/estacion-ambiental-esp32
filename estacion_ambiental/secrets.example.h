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
//      Formato (solo cambia el identificador de tu estación):
//        "https://fisiunap.edu.pe/estacion-ambiental/api/estaciones/estacion-xx/lecturas"
//    - TOKEN_ESTACION: clave secreta de tu estación (40 caracteres).
//      Es personal: no la publiques, no la pongas en fotos ni capturas.
// -----------------------------------------------------------------------------
#define API_URL        "https://fisiunap.edu.pe/estacion-ambiental/api/estaciones/IDENTIFICADOR-DE-TU-ESTACION/lecturas"
#define TOKEN_ESTACION "PEGA_AQUI_EL_TOKEN_QUE_TE_ENVIARON"

// -----------------------------------------------------------------------------
// 3) CERTIFICADOS RAÍZ DEL SERVIDOR  (ya vienen listos: NO los cambies)
//    El ESP32 los usa para comprobar que habla con el servidor real de la FISI
//    (https://fisiunap.edu.pe usa certificados de Let's Encrypt). Son públicos,
//    no son secretos. Solo cámbialos si la coordinación te lo indica.
// -----------------------------------------------------------------------------
static const char ROOT_CA[] =
  // ISRG Root X1 (Let's Encrypt, RSA, vence en 2035)
  "-----BEGIN CERTIFICATE-----\n"
  "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\n"
  "-----END CERTIFICATE-----\n"
  // ISRG Root X2 (Let's Encrypt, ECDSA, vence en 2040)
  "-----BEGIN CERTIFICATE-----\n"
  "MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw\n"
  "CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg\n"
  "R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00\n"
  "MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT\n"
  "ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw\n"
  "EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW\n"
  "+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9\n"
  "ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T\n"
  "AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI\n"
  "zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW\n"
  "tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1\n"
  "/q4AaOeMSQ+2b1tbFfLn\n"
  "-----END CERTIFICATE-----\n";

// -----------------------------------------------------------------------------
// 4) CALIBRACIÓN DE LOS SENSORES MQ-135 Y MQ-7  (la haces tú, ver instrucciones.md, paso 7)
//    - Primera vez: deja MODO_CALIBRACION en 1, carga el programa y anota los dos
//      "R0 promedio" (MQ-135 y MQ-7) que aparecen en el Monitor Serie después de ~20 minutos.
//    - Luego: pon MODO_CALIBRACION en 0 y escribe cada número en su línea.
//    Con un R0 en 0 la estación funciona, pero envía vacíos los gases de ese sensor.
// -----------------------------------------------------------------------------
#define MODO_CALIBRACION        1
#define ESTACION_MQ135_R0_KOHM  0.0   // CO2, amoníaco y alcohol
#define ESTACION_MQ7_R0_KOHM    0.0   // monóxido de carbono (CO)

// -----------------------------------------------------------------------------
// 5) AJUSTES DE HARDWARE  (solo si tu montaje es distinto al de la guía)
// -----------------------------------------------------------------------------
// Divisor de tensión en la salida de cada sensor (dos resistencias iguales = 2.0).
#define ESTACION_MQ135_DIVISOR  2.0
#define ESTACION_MQ7_DIVISOR    2.0

// Opcionales (deja comentados para usar los valores por defecto):
// #define ESTACION_INTERVALO_MS      60000   // ms entre lecturas (por defecto, 1 minuto)
// #define ESTACION_CALENTAMIENTO_MS  180000  // ms de espera inicial de los sensores (3 minutos)
