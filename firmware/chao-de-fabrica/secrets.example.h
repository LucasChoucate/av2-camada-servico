// Copie este arquivo para secrets.h (mesma pasta) e preencha os valores.
// O secrets.h não vai para o GitHub (está no .gitignore).
#pragma once
#include <stdint.h>

// MAC do ESP32 de monitoramento (aparece no Serial dele ao ligar)
static const uint8_t MAC_MONITORAMENTO[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Canal Wi-Fi do roteador ao qual o monitoramento se conecta
// (o monitoramento imprime esse valor no Serial: "Canal do roteador: X")
#define WIFI_CHANNEL 1
