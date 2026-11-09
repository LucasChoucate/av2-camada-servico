# Banco de dados (InfluxDB)

## Configuração

| Item | Valor |
|---|---|
| Versão | [PREENCHER (backend)] |
| Onde roda | [PREENCHER (backend): local, Docker ou InfluxDB Cloud] |
| Endereço | [PREENCHER (backend): ex. http://localhost:8086] |
| Organização | [PREENCHER (backend)] |
| Bucket | [PREENCHER (backend)] |
| Retenção | [PREENCHER (backend): ex. 30 dias] |
| Autenticação | [PREENCHER (backend): como o token foi criado e quais permissões tem. Não colocar o token aqui] |

## Modelo de dados

Measurement: [PREENCHER (backend)]

| Nome | Tipo | Categoria | Descrição |
|---|---|---|---|
| `nivel_tinta` | inteiro | field | Nível do tanque, 0 a 100 % |
| `temperatura` | float | field | Temperatura em °C (DHT22) |
| `umidade` | float | field | Umidade relativa em % (DHT22) |
| `luminosidade` | inteiro | field | Leitura do LDR, 0 a 4095 |
| `uptime_emissor_ms` | inteiro | field | `millis()` do chão de fábrica no momento da leitura |
| `_time` | timestamp | tempo | Hora da leitura (NTP do monitoramento, ou hora do servidor se vier `null`) |
| [PREENCHER (backend): tags ou campos extras, se houver] | | | |

## Fluxo dos dados

1. O chão de fábrica envia a struct por ESP-NOW a cada 2 s.
2. O monitoramento recebe, gera o timestamp ISO em UTC e faz `POST` no backend.
3. O backend valida os campos e grava um ponto no bucket.

## Consultas de exemplo

[PREENCHER (backend): pelo menos uma query (Flux ou SQL/InfluxQL, conforme a versão) que traga as últimas leituras]
