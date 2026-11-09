#include <esp_now.h>
#include <esp_wifi.h>   // [AV2] Necessário para fixar o canal Wi-Fi do ESP-NOW
#include <WiFi.h>
#include <DHT.h>
#include "secrets.h"    // [AV2] MAC do receptor e canal Wi-Fi (arquivo não versionado)

// ================= Definição de Pinos =================
#define TRIG_PIN     4   // Trig do Ultrassônico
#define ECHO_PIN     5   // Echo do Ultrassônico
#define DHTPIN       6   // Pino Digital para o DHT
#define DHTTYPE      DHT22 // DHT22 ou DHT11
#define LDR_PIN      7   // Pino Analógico para o Fotorresistor
#define LED_VERDE    15  // LED Verde (Status OK)
#define LED_VERMELHO 16  // LED Vermelho (Alerta Nível Baixo)

// ================= Configuração do Tanque =================
const int DIST_VAZIO = 100; // Distância quando 0% (em cm)
const int DIST_CHEIO = 10;  // Distância quando 100% (em cm)

DHT dht(DHTPIN, DHTTYPE);

// ================= Configuração ESP-NOW =================
// [AV2] O endereço do receptor agora vem de secrets.h (MAC_MONITORAMENTO).
// Antes era broadcast (FF:FF:FF:FF:FF:FF). Em broadcast o ESP-NOW não recebe
// confirmação de entrega, então o callback sempre informava sucesso e a
// mensagem "Falha no envio ESPNOW" nunca aparecia. Com o MAC real, aparece.

// Estrutura do pacote de dados (Sem PIR)
typedef struct struct_message {
  int nivel_tinta;
  float temperatura;
  float umidade;
  int luminosidade;
  unsigned long timestamp;
} struct_message;

struct_message dadosPacote;
esp_now_peer_info_t peerInfo;

// Controle de tempo
unsigned long tempoAnterior = 0;
const long intervalo = 2000;

bool statusAlerta = false;

// Callback de Envio ESP-NOW (Sem delays para evitar travar o Wi-Fi)
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.printf("Pacote enviado com sucesso: {nível=%d%%, temp=%.1f°C, umidade=%.1f%%, luz=%d}\n",
                  dadosPacote.nivel_tinta, dadosPacote.temperatura,
                  dadosPacote.umidade, dadosPacote.luminosidade);
  } else {
    Serial.println("Falha no envio ESPNOW");
  }
  Serial.println("--------------------------------------------------");
}

// Função para ler a distância do ultrassônico com Timeout
int lerDistancia() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Timeout de 30ms para evitar congelar o loop se o sensor falhar
  long duracao = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duracao == 0) return DIST_VAZIO;

  return (int)(duracao * 0.034 / 2);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- Inicializando ESP32 Chão de Fábrica ---");

  // Configuração dos Pinos
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);

  // Inicializa DHT
  dht.begin();

  // Configura Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // [AV2] Fixa o canal do rádio. O monitoramento fica conectado ao roteador e
  // opera no canal dele; se este ESP32 ficar em outro canal, o ESP-NOW não chega.
  // O monitoramento imprime no Serial o canal correto para colocar em WIFI_CHANNEL.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // [AV2] Mostra o MAC deste ESP32 para configurar MAC_CHAO_FABRICA no monitoramento
  Serial.print("MAC deste ESP32: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("Canal ESP-NOW: %d\n", WIFI_CHANNEL);

  // Inicializa ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }

  // Registra Callback
  esp_now_register_send_cb(OnDataSent);

  // Registra Peer (Receptor)
  memcpy(peerInfo.peer_addr, MAC_MONITORAMENTO, 6);  // [AV2] MAC real do receptor
  peerInfo.channel = WIFI_CHANNEL;                     // [AV2] mesmo canal do roteador
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Falha ao adicionar Peer ESP-NOW");
    return;
  }

  Serial.println("Sistema pronto!");
}

void loop() {
  unsigned long tempoAtual = millis();

  // Executa rotina a cada 2 segundos
  if (tempoAtual - tempoAnterior >= intervalo) {
    tempoAnterior = tempoAtual;

    // 1. Leitura do Ultrassônico (Nível de Tinta)
    int distancia = lerDistancia();
    int porcentagem_tinta = map(distancia, DIST_VAZIO, DIST_CHEIO, 0, 100);
    porcentagem_tinta = constrain(porcentagem_tinta, 0, 100);

    // 2. Leitura do DHT (Temperatura e Umidade)
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    // 3. Leitura do LDR (Luminosidade)
    int luz = analogRead(LDR_PIN);

    // Salva os dados na struct
    dadosPacote.nivel_tinta = porcentagem_tinta;
    dadosPacote.temperatura = isnan(t) ? 0.0 : t;
    dadosPacote.umidade = isnan(h) ? 0.0 : h;
    dadosPacote.luminosidade = luz;
    dadosPacote.timestamp = millis();

    // 4. Lógica de Alerta e LEDs
    if (porcentagem_tinta < 20) {
      statusAlerta = true;
      digitalWrite(LED_VERMELHO, HIGH);
      digitalWrite(LED_VERDE, LOW);
    } else {
      statusAlerta = false;
      digitalWrite(LED_VERMELHO, LOW);
      digitalWrite(LED_VERDE, HIGH);
    }

    // 5. Exibição no Monitor Serial
    Serial.println("\n--- Novas Leituras ---");
    Serial.printf("Nível do tanque: %d%%\n", porcentagem_tinta);

    if (statusAlerta) {
      Serial.println("Alerta! Nível de tinta baixo.");
    }

    if (!isnan(t) && !isnan(h)) {
      Serial.printf("Temperatura: %.0f ºC | Umidade: %.0f%%\n", t, h);
    } else {
      Serial.println("Falha ao ler o sensor DHT!");
    }

    Serial.printf("Luminosidade: %d\n", luz);

    if (statusAlerta) {
      Serial.println("Estado: Alerta – verificar tanque de tinta");
    } else {
      Serial.println("Estado: Operação normal");
    }

    // 6. Transmissão via ESP-NOW (reenvia a cada ciclo; uma falha não trava o loop)
    esp_err_t result = esp_now_send(MAC_MONITORAMENTO, (uint8_t *) &dadosPacote, sizeof(dadosPacote));  // [AV2]
    if (result != ESP_OK) {
      Serial.println("Erro ao disparar envio ESP-NOW.");
    }
  }
}
