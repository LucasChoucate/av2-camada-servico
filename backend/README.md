# Backend

API em Python que recebe as leituras do ESP32 de monitoramento e grava no InfluxDB.

Responsável: [PREENCHER]

## O que o firmware espera

O ESP32 de monitoramento faz um `POST` para o endereço configurado em `BACKEND_URL` (arquivo `firmware/monitoramento/secrets.h`) a cada pacote recebido, ou seja, a cada 2 s.

```
POST /api/leituras
Content-Type: application/json

{
  "nivel_tinta": 75,
  "temperatura": 24.10,
  "umidade": 55.00,
  "luminosidade": 680,
  "timestamp": "2025-09-02T14:35:00Z",
  "uptime_emissor_ms": 123456
}
```

- `timestamp` pode chegar como `null` se o NTP ainda não sincronizou. Nesse caso use a hora do servidor.
- Qualquer resposta 2xx é tratada como sucesso. O ESP32 espera no máximo 1,5 s pela resposta.
- A API precisa escutar em `0.0.0.0` (não em `localhost`) para o ESP32 alcançar o computador pela rede.

Se a rota ou os nomes dos campos forem diferentes, ajuste `BACKEND_URL` e a função `enviarParaBackend()` em `firmware/monitoramento/monitoramento.ino`.

## Como rodar

[PREENCHER: pré-requisitos, criação do ambiente virtual, `pip install -r requirements.txt`, variáveis do `.env` (com um `.env.example` sem valores reais) e comando para iniciar]

## Rotas

| Método | Rota | Descrição |
|---|---|---|
| POST | `/api/leituras` | Recebe uma leitura e grava no InfluxDB |
| [PREENCHER] | [PREENCHER] | [PREENCHER] |
