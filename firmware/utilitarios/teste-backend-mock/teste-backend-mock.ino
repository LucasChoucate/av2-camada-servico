// Teste do backend com um único ESP32-S3, SEM nenhum sensor ligado.
//
// Todos os dados são simulados com valores aleatórios:
// - Ultrassônico: a distância até a tinta aumenta aos poucos (o tanque esvazia) com um ruído aleatório.
//   Quando o nível fica muito baixo, o tanque é "reabastecido". Assim o alerta de nível < 20% também aparece.
// - Temperatura, umidade e luminosidade: passeio aleatório, variando aos poucos como um sensor real.
// - A cada 2 s mostra tudo no Monitor Serial (115200) e depois envia por HTTPS para a API
//   (POST /api/leituras), no mesmo formato JSON do firmware de monitoramento.
//
// Como usar:
//   1. Preencha WIFI_SSID e WIFI_PASSWORD logo abaixo dos includes.
//   2. Placa "ESP32S3 Dev Module" (esp32 by Espressif 3.x). Para ver o Serial pela porta USB nativa,
//      ative Tools > USB CDC On Boot > Enabled.
//   3. Grave e abra o Monitor Serial em 115200.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
// ---------------------------------------------------------------- rede
// Wi-Fi 2.4 GHz (o ESP32-S3 não conecta em 5 GHz)
const char *WIFI_SSID = "NOME_DA_REDE";
const char *WIFI_PASSWORD = "SENHA_DA_REDE";

// API no Render (precisa ser https://)
const char *BACKEND_URL = "https://pintura-iot.onrender.com/api/leituras";

// ---------------------------------------------------------------- configuração
const int DIST_VAZIO = 100;  // distância (cm) com o tanque vazio  -> 0%
const int DIST_CHEIO = 10;   // distância (cm) com o tanque cheio  -> 100%
const int NIVEL_ALERTA = 20; // abaixo disso (%) é alerta

const unsigned long INTERVALO_MS = 2000;  // leitura + envio a cada 2 s
const uint16_t HTTP_TIMEOUT_MS = 10000;   // HTTPS no Render pode passar de 1,5 s
const char *DEVICE_ID = "esp32s3-mock";  // aparece na coluna "Dispositivo" do Grafana

// ---------------------------------------------------------------- estado
WiFiClientSecure clienteTls;
HTTPClient http;

float distanciaSimulada = 20.0f;  // começa com o tanque quase cheio (~89%)
float temperatura = 24.0f;        // valores iniciais da simulação
float umidade = 55.0f;
int luminosidade = 680;

unsigned long ultimoCiclo = 0;
unsigned long numeroLeitura = 0;
unsigned long enviosOk = 0;
unsigned long enviosFalha = 0;

// ---------------------------------------------------------------- utilidades
float limitar(float valor, float minimo, float maximo) {
  if (valor < minimo) return minimo;
  if (valor > maximo) return maximo;
  return valor;
}

// Número aleatório entre minimo e maximo (esp_random usa o gerador de hardware do ESP32)
float aleatorio(float minimo, float maximo) {
  return minimo + (maximo - minimo) * (esp_random() / 4294967295.0f);
}

// ---------------------------------------------------------------- sensores
// Ultrassônico simulado: o tanque esvazia aos poucos, com ruído de medição.
// Perto de vazio (distância > 95 cm, nível < ~6%), o tanque é reabastecido.
float simularDistanciaCm() {
  distanciaSimulada += aleatorio(0.0f, 1.2f);  // consumo de tinta a cada ciclo
  if (distanciaSimulada > 95.0f) {
    distanciaSimulada = aleatorio(10.0f, 18.0f);  // reabastecimento
    Serial.println(">> Simulação: tanque reabastecido");
  }
  return limitar(distanciaSimulada + aleatorio(-0.5f, 0.5f), 2.0f, 400.0f);  // ruído do sensor
}

// Converte a distância em % de tinta: DIST_VAZIO = 0%, DIST_CHEIO = 100%
int calcularNivel(float distanciaCm) {
  float pct = (DIST_VAZIO - distanciaCm) * 100.0f / (DIST_VAZIO - DIST_CHEIO);
  return (int)lroundf(limitar(pct, 0.0f, 100.0f));
}

// Sensores simulados: cada ciclo varia um pouco em relação ao anterior
void simularSensores() {
  temperatura = limitar(temperatura + aleatorio(-0.4f, 0.4f), 18.0f, 32.0f);
  umidade = limitar(umidade + aleatorio(-1.5f, 1.5f), 35.0f, 80.0f);
  luminosidade = (int)limitar(luminosidade + aleatorio(-60.0f, 60.0f), 0.0f, 4095.0f);
}

// ---------------------------------------------------------------- rede
void conectarWiFi() {
  Serial.printf("Conectando ao Wi-Fi \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Wi-Fi conectado. IP: %s | RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("Não conectou ao Wi-Fi (rede 2.4 GHz? senha certa?). Vou tentar de novo no loop.");
  }
}

void sincronizarHora() {
  configTime(0, 0, "pool.ntp.org", "time.google.com");  // UTC
  Serial.print("Sincronizando hora (NTP)");
  unsigned long inicio = millis();
  while (time(nullptr) < 1700000000 && millis() - inicio < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(time(nullptr) >= 1700000000 ? " OK" : " sem resposta (o backend usará a hora dele)");
}

// Hora atual em ISO 8601 UTC. Retorna false se o NTP ainda não sincronizou.
bool timestampISO(char *buf, size_t tamanho) {
  time_t agora = time(nullptr);
  if (agora < 1700000000) return false;
  struct tm t;
  gmtime_r(&agora, &t);
  strftime(buf, tamanho, "%Y-%m-%dT%H:%M:%SZ", &t);
  return true;
}

// POST do JSON para a API. Retorna true se gravou (HTTP 2xx).
bool enviarParaBackend(const char *json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Envio: Wi-Fi desconectado, tentando reconectar...");
    WiFi.reconnect();
    return false;
  }

  unsigned long inicio = millis();
  if (!http.begin(clienteTls, BACKEND_URL)) {
    Serial.println("Envio: BACKEND_URL inválida");
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  int codigo = http.POST((uint8_t *)json, strlen(json));
  unsigned long duracao = millis() - inicio;

  bool ok = codigo >= 200 && codigo < 300;
  if (codigo > 0) {
    String resposta = http.getString();
    Serial.printf("Resposta: HTTP %d em %lu ms -> %s\n", codigo, duracao, resposta.c_str());
  } else {
    Serial.printf("Falha no envio após %lu ms: %s\n", duracao, http.errorToString(codigo).c_str());
    Serial.println("  (se for o primeiro envio, o Render pode estar acordando o serviço: leva até ~1 min)");
  }
  http.end();  // com setReuse(true), a conexão TLS fica aberta para o próximo envio
  return ok;
}

// ---------------------------------------------------------------- ciclo principal
void cicloLeituraEnvio() {
  numeroLeitura++;
  Serial.printf("\n========== Leitura #%lu | uptime %lu s ==========\n", numeroLeitura, millis() / 1000);

  // 1) Ultrassônico simulado
  float distancia = simularDistanciaCm();
  int nivel = calcularNivel(distancia);

  // 2) Demais sensores simulados
  simularSensores();

  // 3) Tudo no Monitor Serial
  Serial.printf("Distância: %.1f cm  (simulado)\n", distancia);
  Serial.printf("Nível do tanque: %d%%\n", nivel);
  Serial.printf("Temperatura: %.1f ºC | Umidade: %.0f%%  (simulados)\n", temperatura, umidade);
  Serial.printf("Luminosidade: %d  (simulado)\n", luminosidade);
  if (nivel < NIVEL_ALERTA) {
    Serial.println("Alerta! Nível de tinta baixo.");
    Serial.println("Estado: Alerta - verificar tanque de tinta");
  } else {
    Serial.println("Estado: Operação normal");
  }

  char ts[25];
  bool temHora = timestampISO(ts, sizeof(ts));
  Serial.printf("Timestamp: %s\n", temHora ? ts : "sem NTP (null)");

  // 4) Monta o JSON (mesmo formato do firmware de monitoramento) e envia
  char json[256];
  snprintf(json, sizeof(json),
           "{\"nivel_tinta\":%d,\"temperatura\":%.2f,\"umidade\":%.2f,\"luminosidade\":%d,"
           "\"timestamp\":%s%s%s,\"uptime_emissor_ms\":%lu,\"device_id\":\"%s\"}",
           nivel, temperatura, umidade, luminosidade,
           temHora ? "\"" : "", temHora ? ts : "null", temHora ? "\"" : "",
           millis(), DEVICE_ID);
  Serial.printf("Enviando para %s\n%s\n", BACKEND_URL, json);

  if (enviarParaBackend(json)) enviosOk++;
  else enviosFalha++;
  Serial.printf("Envios: %lu ok / %lu falha(s)\n", enviosOk, enviosFalha);
}

void setup() {
  Serial.begin(115200);
  unsigned long inicio = millis();
  while (!Serial && millis() - inicio < 3000) delay(10);  // espera o USB CDC do S3
  delay(500);
  Serial.println("\n=== Teste do backend: todos os sensores simulados ===");

  conectarWiFi();
  sincronizarHora();

  clienteTls.setInsecure();  // não valida o certificado do Render (simplifica o teste)
  http.setReuse(true);       // reaproveita a conexão HTTPS entre envios
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  Serial.printf("Destino: %s | intervalo: %lu ms\n", BACKEND_URL, INTERVALO_MS);
}

void loop() {
  if (millis() - ultimoCiclo >= INTERVALO_MS) {
    ultimoCiclo = millis();
    cicloLeituraEnvio();
  }
}
