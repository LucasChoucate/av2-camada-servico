# Monitoramento do Processo de Pintura com ESP32, ESP-NOW, InfluxDB e Grafana

Projeto da AV2 da disciplina **Práticas Integradas: Camada de Serviço**.

Sistema IoT que acompanha o processo de pintura de blocos de madeira em um chão de fábrica simulado. Um ESP32 lê os sensores do tanque de tinta e envia as leituras por ESP-NOW para um segundo ESP32, que mostra os valores numa matriz de LEDs e encaminha tudo para um backend em Python. O backend grava no InfluxDB e o Grafana exibe os dashboards em tempo real.

## Integrantes

- Felipe Nogueira
- Lucas Choucate
- Rafael Cezimbra 
- Ramon Papes


## Arquitetura

```mermaid
flowchart LR
    S[Sensores<br/>HC-SR04, DHT22, LDR] --> A[ESP32-S3<br/>chão de fábrica]
    A -- ESP-NOW a cada 2 s --> B[ESP32-S3<br/>monitoramento]
    B --> M[Matriz de LEDs 8x8]
    B -- HTTP POST JSON --> API[Backend Python]
    API --> DB[(InfluxDB)]
    DB --> G[Grafana]
```

## Estrutura do repositório

```
.
├── firmware/
│   ├── chao-de-fabrica/      # ESP32 que lê os sensores e envia por ESP-NOW
│   ├── monitoramento/        # ESP32 que recebe, exibe na matriz e envia ao backend
│   └── utilitarios/ler-mac/  # Sketch auxiliar para descobrir o MAC de cada placa
├── backend/                  # API em Python que grava no InfluxDB
├── docs/
│   ├── circuito.md           # Ligações de cada componente
│   ├── banco-de-dados.md     # Descrição do InfluxDB
│   └── grafana.md            # Descrição dos dashboards
└── README.md
```

## Materiais

| Item | Quantidade |
|---|---|
| ESP32-S3 | 2 |
| Sensor ultrassônico HC-SR04 | 1 |
| Sensor de temperatura e umidade DHT22 | 1 |
| Fotorresistor (LDR) | 1 |
| Matriz de LEDs 8x8 com MAX7219 (1088AS) | 1 |
| LED verde | 1 |
| LED vermelho | 1 |
| Resistores, protoboard e jumpers | diversos |
| Computador com Arduino IDE, Python, InfluxDB e Grafana | 1 |

## Como montar o projeto

A pinagem completa está em [docs/circuito.md](docs/circuito.md). Resumo:

| ESP32 | Componente | GPIO |
|---|---|---|
| Chão de fábrica | HC-SR04 TRIG / ECHO | 4 / 5 |
| Chão de fábrica | DHT22 | 6 |
| Chão de fábrica | LDR | 7 |
| Chão de fábrica | LED verde / LED vermelho | 15 / 16 |
| Monitoramento | Matriz DIN / CS / CLK | 13 / 14 / 15 |

### 1. Preparar a Arduino IDE

1. Instale a placa **esp32 by Espressif Systems** (versão 3.x recente) pelo Gerenciador de Placas. O firmware usa a assinatura nova do callback de envio do ESP-NOW (`wifi_tx_info_t`), que não existe nas versões 2.x.
2. Instale as bibliotecas pelo Gerenciador de Bibliotecas:
   - **DHT sensor library** (Adafruit), que também pede a **Adafruit Unified Sensor**
   - **MD_Parola** e **MD_MAX72XX** (majicDesigns)
3. Selecione a placa ESP32S3 Dev Module.

### 2. Descobrir o MAC de cada placa

Grave `firmware/utilitarios/ler-mac/ler-mac.ino` em cada ESP32 e abra o Monitor Serial (115200). Anote a linha "Para ESP-NOW" de cada um.

### 3. Configurar os segredos

Em cada pasta de firmware, copie `secrets.example.h` para `secrets.h` e preencha. O `secrets.h` está no `.gitignore` e não deve ir para o GitHub.

| Arquivo | Campo | O que colocar |
|---|---|---|
| `monitoramento/secrets.h` | `WIFI_SSID`, `WIFI_PASSWORD` | Rede Wi-Fi 2.4 GHz |
| `monitoramento/secrets.h` | `BACKEND_URL` | `http://IP_DO_COMPUTADOR:PORTA/api/leituras` |
| `monitoramento/secrets.h` | `MAC_CHAO_FABRICA` | MAC do ESP32 chão de fábrica |
| `chao-de-fabrica/secrets.h` | `MAC_MONITORAMENTO` | MAC do ESP32 de monitoramento |
| `chao-de-fabrica/secrets.h` | `WIFI_CHANNEL` | Canal do roteador (ver passo 4) |

### 4. Gravar os firmwares

1. Grave o **monitoramento** primeiro. Ao conectar no Wi-Fi ele imprime no Serial a linha `Canal do roteador: X`.
2. Coloque esse número em `WIFI_CHANNEL` no `secrets.h` do chão de fábrica. Os dois ESP32 precisam estar no mesmo canal, senão o ESP-NOW não chega.
3. Grave o **chão de fábrica**.

## Funcionamento

**Chão de fábrica.** A cada 2 s lê o nível do tanque (100 cm = 0%, 10 cm = 100%), a temperatura, a umidade e a luminosidade, mostra tudo no Monitor Serial e envia o pacote por ESP-NOW. LED verde aceso indica operação normal; abaixo de 20% de tinta acende o vermelho e o Serial mostra "Alerta! Nível de tinta baixo.".

**Monitoramento.** Aceita pacotes apenas do MAC do chão de fábrica. Exibe em rolagem na matriz as telas `NVL`, `TMP`, `UMD` e `LUX`. Se ficar mais de 5 s sem receber, mostra `SEM DADOS`. Cada pacote recebido ganha um timestamp ISO 8601 em UTC (hora obtida por NTP) e é enviado ao backend por HTTP.

Formato enviado ao backend:

```json
{
  "nivel_tinta": 75,
  "temperatura": 24.10,
  "umidade": 55.00,
  "luminosidade": 680,
  "timestamp": "2025-09-02T14:35:00Z",
  "uptime_emissor_ms": 123456
}
```

O `timestamp` vem como `null` enquanto o NTP não sincroniza; nesse caso o backend usa a própria hora.

## Como rodar o backend

[PREENCHER (backend): pré-requisitos, como criar o ambiente virtual, instalar dependências, configurar o `.env` e o comando para iniciar a API. Detalhes em [backend/README.md](backend/README.md).]

## Banco de dados e Grafana

- Descrição do banco: [docs/banco-de-dados.md](docs/banco-de-dados.md)
- Descrição dos dashboards: [docs/grafana.md](docs/grafana.md)

## Como consultar os dados salvos

[PREENCHER (backend): como ver os dados no InfluxDB (interface web, query de exemplo) e, se existir, a rota da API que retorna as últimas leituras.]

## Dificuldades e soluções

### 1. [PREENCHER: título da dificuldade]

**Problema:** [PREENCHER]

**Solução:** [PREENCHER]

### 2. [PREENCHER: título da dificuldade]

**Problema:** [PREENCHER]

**Solução:** [PREENCHER]

### 3. [PREENCHER: título da dificuldade]

**Problema:** [PREENCHER]

**Solução:** [PREENCHER]

## Observações

- O sensor de presença (PIR) e os LEDs de status do ESP32 de monitoramento foram dispensados pelo professor e não fazem parte do projeto.
- Os dois controladores são ESP32-S3. Num ESP32 comum os GPIO 6 e 7 são ligados à memória flash e não podem ser usados para sensores.
