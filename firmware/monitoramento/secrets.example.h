// Copie este arquivo para secrets.h (mesma pasta) e preencha os valores.
// O secrets.h não vai para o GitHub (está no .gitignore).
#pragma once
#include <stdint.h>

// Rede Wi-Fi (2.4 GHz) usada para falar com o backend
#define WIFI_SSID     "NOME_DA_REDE"
#define WIFI_PASSWORD "SENHA_DA_REDE"

// Endereço do backend: IP do computador que roda o FastAPI, na mesma rede
#define BACKEND_URL   "http://192.168.0.10:8000/api/leituras"

// MAC do ESP32 chão de fábrica (aparece no Serial dele ao ligar).
// Só pacotes vindos deste MAC são aceitos.
static const uint8_t MAC_CHAO_FABRICA[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
