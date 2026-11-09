// Utilitário: mostra o MAC de fábrica do ESP32 (interface Wi-Fi Station).
// É esse MAC que o ESP-NOW usa. Grave nos dois ESP32 e anote os valores:
//  - MAC do monitoramento  -> MAC_MONITORAMENTO no secrets.h do chão de fábrica
//  - MAC do chão de fábrica -> MAC_CHAO_FABRICA no secrets.h do monitoramento
#include <Arduino.h>
#include <esp_mac.h>    // Nativo no ESP32 Core 3.x
#include <esp_system.h> // Compatibilidade retroativa

void setup() {
  Serial.begin(115200);
  delay(1500); // Aguarda o monitor serial da VM sincronizar

  uint8_t mac[6];

  // Lê diretamente dos fusíveis de fábrica (eFuse) do hardware
  esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);

  Serial.println("\n=== ENDEREÇO MAC DE FÁBRICA (eFuse) ===");
  if (ret == ESP_OK) {
    Serial.printf("MAC Formatado : %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("Para ESP-NOW  : {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  } else {
    Serial.println("Erro ao ler os eFuses do ESP32.");
  }
  Serial.println("========================================");
}

void loop() {
  // Nada a executar repetidamente
}
