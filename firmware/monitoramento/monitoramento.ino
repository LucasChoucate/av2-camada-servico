#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>   // [AV2] Envio das leituras para o backend
#include <time.h>         // [AV2] Hora via NTP para o timestamp ISO
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include "secrets.h"      // [AV2] Wi-Fi, URL do backend e MAC autorizado (não versionado)

// ================= Configuração da Matriz 1088AS (ESP32-S3) =================
// SE O TEXTO CONTINUAR EMBARALHADO, MUDE DE GENERIC_HW PARA PAROLA_HW
#define HARDWARE_TYPE MD_MAX72XX::GENERIC_HW
#define MAX_DEVICES 1   // 1 Matriz 8x8 (1088AS)

// Pinos corretos que acenderam a matriz no ESP32-S3
#define DATA_PIN 13   // DIN
#define CS_PIN   14   // CS
#define CLK_PIN  15   // CLK

// [AV2] Tempo máximo de cada requisição HTTP, para não travar a matriz
const uint16_t HTTP_TIMEOUT_MS = 1500;

// Construtor correto passando os pinos
MD_Parola Display = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// Struct do pacote de dados (20 bytes)
typedef struct struct_message {
  int nivel_tinta;
  float temperatura;
  float umidade;
  int luminosidade;
  unsigned long timestamp;
} struct_message;

struct_message dadosRecebidos;

// [AV2] O callback do ESP-NOW roda na tarefa do Wi-Fi e o loop roda em outra.
// Este mutex evita ler a struct pela metade enquanto ela é sobrescrita.
portMUX_TYPE muxDados = portMUX_INITIALIZER_UNLOCKED;
volatile bool novoPacote = false;
volatile time_t horaRecebimento = 0;

// Variáveis de controle de tempo
volatile unsigned long ultimoRecebimento = 0;
const unsigned long TIMEOUT_MS = 5000;

int telaAtual = 0;
char bufferTexto[25] = "INICIANDO";
bool timeoutExibido = false;

// Customização de Fonte Ultra-Fina
uint8_t f_0[] = {3, 0x3E, 0x41, 0x3E};
uint8_t f_1[] = {2, 0x00, 0x7F};
uint8_t f_2[] = {3, 0x4B, 0x49, 0x31};
uint8_t f_3[] = {3, 0x22, 0x49, 0x36};
uint8_t f_4[] = {3, 0x1C, 0x10, 0x7F};
uint8_t f_5[] = {3, 0x39, 0x49, 0x26};
uint8_t f_6[] = {3, 0x3E, 0x49, 0x26};
uint8_t f_7[] = {3, 0x40, 0x47, 0x48};
uint8_t f_8[] = {3, 0x36, 0x49, 0x36};
uint8_t f_9[] = {3, 0x32, 0x49, 0x3E};

uint8_t f_N[] = {3, 0x7F, 0x20, 0x7F};
uint8_t f_V[] = {3, 0x3E, 0x40, 0x3E};
uint8_t f_L[] = {3, 0x7F, 0x01, 0x01};
uint8_t f_T[] = {3, 0x40, 0x7F, 0x40};
// [AV2] O M tinha o mesmo desenho do N. Agora tem 5 colunas e fica legível.
uint8_t f_M[] = {5, 0x7F, 0x20, 0x10, 0x20, 0x7F};
uint8_t f_P[] = {3, 0x7F, 0x48, 0x30};
uint8_t f_U[] = {3, 0x3E, 0x01, 0x3E};
uint8_t f_D[] = {3, 0x7F, 0x41, 0x3E};
uint8_t f_X[] = {3, 0x63, 0x1C, 0x63};
uint8_t f_C[] = {3, 0x3E, 0x41, 0x41};
// [AV2] Letras extras para a tela de alerta "SEM DADOS"
uint8_t f_S[] = {3, 0x32, 0x49, 0x26};
uint8_t f_E[] = {3, 0x7F, 0x49, 0x41};
uint8_t f_A[] = {3, 0x3F, 0x48, 0x3F};
uint8_t f_O[] = {3, 0x3E, 0x41, 0x3E};
uint8_t f_pct[] = {3, 0x22, 0x10, 0x22}; // % fino
uint8_t f_espaco[] = {2, 0x00, 0x00};   // Espaço fino

void carregarFonteFina() {
  Display.addChar('0', f_0); Display.addChar('1', f_1);
  Display.addChar('2', f_2); Display.addChar('3', f_3);
  Display.addChar('4', f_4); Display.addChar('5', f_5);
  Display.addChar('6', f_6); Display.addChar('7', f_7);
  Display.addChar('8', f_8); Display.addChar('9', f_9);

  Display.addChar('N', f_N); Display.addChar('V', f_V);
  Display.addChar('L', f_L); Display.addChar('T', f_T);
  Display.addChar('M', f_M); Display.addChar('P', f_P);
  Display.addChar('U', f_U); Display.addChar('D', f_D);
  Display.addChar('X', f_X); Display.addChar('C', f_C);
  Display.addChar('S', f_S); Display.addChar('E', f_E);  // [AV2]
  Display.addChar('A', f_A); Display.addChar('O', f_O);  // [AV2]
  Display.addChar('%', f_pct);
  Display.addChar(' ', f_espaco);
}

// [AV2] Converte a hora para ISO 8601 em UTC (ex.: 2025-09-02T14:35:00Z).
// Retorna false se o NTP ainda não sincronizou o relógio.
bool formatarISO(time_t t, char *saida, size_t tamanho) {
  if (t < 1700000000) return false;
  struct tm tmUTC;
  gmtime_r(&t, &tmUTC);
  strftime(saida, tamanho, "%Y-%m-%dT%H:%M:%SZ", &tmUTC);
  return true;
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  // [AV2] Aceita apenas pacotes do coletor autorizado
  if (memcmp(recv_info->src_addr, MAC_CHAO_FABRICA, 6) != 0) {
    const uint8_t *m = recv_info->src_addr;
    Serial.printf("Pacote ignorado: MAC %02X:%02X:%02X:%02X:%02X:%02X não autorizado\n",
                  m[0], m[1], m[2], m[3], m[4], m[5]);
    return;
  }

  if (len != sizeof(struct_message)) {
    Serial.printf("Pacote ignorado: tamanho %d inesperado\n", len);
    return;
  }

  // [AV2] O callback só copia os dados e levanta a flag; o envio HTTP fica no loop
  portENTER_CRITICAL(&muxDados);
  memcpy(&dadosRecebidos, incomingData, sizeof(dadosRecebidos));
  ultimoRecebimento = millis();
  horaRecebimento = time(nullptr);
  novoPacote = true;
  portEXIT_CRITICAL(&muxDados);

  timeoutExibido = false;
}

// [AV2] Registra a recepção no Serial e envia a leitura para o backend
void enviarParaBackend(const struct_message &d, time_t hora) {
  char ts[25];
  bool temHora = formatarISO(hora, ts, sizeof(ts));

  Serial.printf("RX: nivel=%d%% temp=%.0fC umd=%.0f%% lux=%d ts=%s\n",
                d.nivel_tinta, d.temperatura, d.umidade, d.luminosidade,
                temHora ? ts : "sem-ntp");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Backend: Wi-Fi desconectado, leitura não enviada");
    return;
  }

  // Sem hora sincronizada, manda null e o backend usa a hora dele
  char json[256];
  snprintf(json, sizeof(json),
           "{\"nivel_tinta\":%d,\"temperatura\":%.2f,\"umidade\":%.2f,"
           "\"luminosidade\":%d,\"timestamp\":%s%s%s,\"uptime_emissor_ms\":%lu}",
           d.nivel_tinta, d.temperatura, d.umidade, d.luminosidade,
           temHora ? "\"" : "", temHora ? ts : "null", temHora ? "\"" : "",
           d.timestamp);

  HTTPClient http;
  if (!http.begin(BACKEND_URL)) {
    Serial.println("Backend: URL inválida em BACKEND_URL");
    return;
  }
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  int codigo = http.POST(json);
  if (codigo >= 200 && codigo < 300) {
    Serial.printf("Backend: leitura gravada (HTTP %d)\n", codigo);
  } else if (codigo > 0) {
    Serial.printf("Backend: recusou a leitura (HTTP %d): %s\n", codigo, http.getString().c_str());
  } else {
    Serial.printf("Backend: falha de conexão (%s)\n", http.errorToString(codigo).c_str());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);

  // Inicialização da Matriz
  Display.begin();
  Display.setIntensity(8);

  // =================================================================
  // PASSO 1: Ligar tudo para garantir que o chip acordou
  // =================================================================
  Display.getGraphicObject()->control(MD_MAX72XX::TEST, MD_MAX72XX::ON);
  delay(1000);

  // =================================================================
  // PASSO 2: Apagar e resetar a fonte
  // =================================================================
  Display.getGraphicObject()->control(MD_MAX72XX::TEST, MD_MAX72XX::OFF);
  Display.displayClear();
  Display.setFont(nullptr);

  // =================================================================
  // PASSO 3: Carregar a fonte fina do usuário
  // =================================================================
  carregarFonteFina();

  // Ajustado para PA_LEFT para rolar perfeitamente desde fora da tela
  // Velocidade = 110ms (mais lento), pausa final = 0
  Display.displayText(bufferTexto, PA_LEFT, 110, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);

  // Configuração ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // [AV2] Sem economia de energia, para não perder pacotes ESP-NOW

  // [AV2] Conexão com o Wi-Fi para falar com o backend
  Serial.print("MAC deste ESP32 (use em MAC_MONITORAMENTO no chão de fábrica): ");
  Serial.println(WiFi.macAddress());

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando ao Wi-Fi");
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Wi-Fi conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Canal do roteador: %d (use este valor em WIFI_CHANNEL no chão de fábrica)\n",
                  WiFi.channel());
    configTime(0, 0, "pool.ntp.org", "time.google.com");  // [AV2] hora em UTC
  } else {
    Serial.println("Wi-Fi não conectou. O ESP-NOW segue funcionando, mas nada vai para o backend.");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP32-S3 Monitoramento (1088AS): Aguardando pacotes...");
}

void loop() {
  // [AV2] Tira uma cópia consistente dos dados compartilhados com o callback
  struct_message dados;
  bool temNovo;
  time_t hora;
  unsigned long ultimo;
  portENTER_CRITICAL(&muxDados);
  dados = dadosRecebidos;
  temNovo = novoPacote;
  novoPacote = false;
  hora = horaRecebimento;
  ultimo = ultimoRecebimento;
  portEXIT_CRITICAL(&muxDados);

  unsigned long tempoAtual = millis();
  bool emTimeout = (ultimo == 0) || (tempoAtual - ultimo > TIMEOUT_MS);

  if (temNovo) {
    enviarParaBackend(dados, hora);   // [AV2]
  }

  if (emTimeout) {
    if (!timeoutExibido) {
      // [AV2] Tela de alerta pedida no edital. Todas as letras de "SEM DADOS"
      // estão na fonte fina, então não mistura com a fonte padrão.
      snprintf(bufferTexto, sizeof(bufferTexto), "SEM DADOS");
      Display.displayReset();
      timeoutExibido = true;
      Serial.println("Tela -> SEM DADOS");
    }
  }

  if (Display.displayAnimate()) {
    if (!emTimeout) {
      switch (telaAtual) {
        case 0:
          snprintf(bufferTexto, sizeof(bufferTexto), "NVL %d%%", dados.nivel_tinta);
          Serial.println("Tela -> NVL");   // [AV2]
          break;
        case 1:
          snprintf(bufferTexto, sizeof(bufferTexto), "TMP %.0fC", dados.temperatura);
          Serial.println("Tela -> TMP");   // [AV2]
          break;
        case 2:
          snprintf(bufferTexto, sizeof(bufferTexto), "UMD %.0f%%", dados.umidade);
          Serial.println("Tela -> UMD");   // [AV2]
          break;
        case 3:
          snprintf(bufferTexto, sizeof(bufferTexto), "LUX %d", dados.luminosidade);
          Serial.println("Tela -> LUX");   // [AV2]
          break;
      }

      telaAtual = (telaAtual + 1) % 4;
    }

    Display.displayReset();
  }
}
