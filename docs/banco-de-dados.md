# Banco de dados (SQLite)

O edital sugere o InfluxDB. O grupo usou o **SQLite**: o banco inteiro é um único arquivo, não precisa de
servidor próprio e roda no mesmo container da API e do Grafana. Isso permite hospedar tudo em um único
serviço do Render. O código está em [backend/app/database.py](../backend/app/database.py).

## Configuração

| Item | Valor |
|---|---|
| Versão | SQLite 3.45 (módulo `sqlite3` do Python, na imagem Ubuntu 24.04 do Grafana) |
| Onde roda | Dentro do container Docker do backend, no Render ou local com `docker compose` |
| Endereço | Arquivo `/data/pintura.db` (variável `DATABASE_PATH`). Não há porta: a escrita passa pela API (`POST /api/leituras`) |
| Organização / Bucket | Não se aplica. Os dados ficam na tabela `leituras` |
| Retenção | 30 dias (variável `RETENTION_DAYS`). A API apaga as leituras mais antigas a cada hora; `0` guarda para sempre |
| Autenticação | Sem chave: o ESP32 grava sem token. Se a variável `API_KEY` for definida, a API passa a exigir o header `X-API-Key`. O Grafana abre o arquivo em modo somente leitura |
| Concorrência | Modo WAL: o Grafana lê enquanto a API grava |

No plano gratuito do Render, o arquivo é apagado a cada deploy ou reinício do serviço. Para manter o
histórico, é preciso um disco persistente montado em `/data`, que só existe nos planos pagos (bloco `disk`
comentado no [render.yaml](../render.yaml)).

## Modelo de dados

Tabela: `leituras` (uma linha por leitura recebida)

| Nome | Tipo | Obrigatório | Descrição |
|---|---|---|---|
| `id` | INTEGER | sim | Chave primária, autoincremento |
| `ts` | INTEGER | sim | Hora da leitura em segundos Unix (UTC): o `timestamp` do NTP do monitoramento, ou a hora do servidor se vier `null` |
| `recebido_em` | INTEGER | sim | Hora em que a API recebeu a leitura (segundos Unix, UTC) |
| `device_id` | TEXT | sim | Identificador do emissor (padrão `chao-fabrica-01`) |
| `nivel_tinta` | REAL | sim | Nível do tanque, 0 a 100 % |
| `temperatura` | REAL | não | Temperatura em °C (DHT22). `NULL` se o sensor falhar (`nan`) |
| `umidade` | REAL | não | Umidade relativa em % (DHT22). `NULL` se o sensor falhar (`nan`) |
| `luminosidade` | INTEGER | sim | Leitura do LDR, 0 a 4095 |
| `presenca` | INTEGER | não | Reservado para o PIR (0/1). Fica `NULL` porque o PIR foi dispensado |
| `uptime_emissor_ms` | INTEGER | não | `millis()` do chão de fábrica no momento da leitura |
| `alerta` | INTEGER | sim | `1` se `nivel_tinta` < 20 % (variável `NIVEL_ALERTA`), senão `0` |

Índices: `ts` e (`device_id`, `ts`), para os filtros por período do Grafana e da API.

A API valida os campos antes de gravar. Uma leitura fora da faixa (ex.: `nivel_tinta` = 150) é recusada
com HTTP 422.

## Fluxo dos dados

1. O chão de fábrica envia a struct por ESP-NOW a cada 2 s.
2. O monitoramento recebe, gera o timestamp ISO em UTC e faz `POST /api/leituras` no backend.
3. O backend valida os campos, converte o timestamp para segundos Unix, calcula o `alerta` e insere uma linha em `leituras`.
4. O Grafana consulta o mesmo arquivo a cada 5 s.

## Consultas de exemplo

Pelo terminal do container: `sqlite3 /data/pintura.db`.

Últimas 10 leituras:

```sql
SELECT datetime(ts, 'unixepoch') AS hora, nivel_tinta, temperatura, umidade, luminosidade
FROM leituras
ORDER BY ts DESC
LIMIT 10;
```

Resumo da última hora:

```sql
SELECT COUNT(*)                   AS leituras,
       MIN(nivel_tinta)           AS nivel_min,
       ROUND(AVG(nivel_tinta), 1) AS nivel_medio,
       ROUND(AVG(temperatura), 1) AS temp_media,
       SUM(alerta)                AS leituras_em_alerta
FROM leituras
WHERE ts >= CAST(strftime('%s', 'now', '-1 hour') AS INTEGER);
```

Momentos em que o tanque entrou em alerta:

```sql
SELECT datetime(ts, 'unixepoch') AS hora, nivel_tinta
FROM leituras
WHERE alerta = 1
ORDER BY ts DESC
LIMIT 20;
```

Sem acesso ao terminal, as mesmas informações saem pela API: `GET /api/leituras?limit=10`,
`GET /api/resumo?horas=1` e `GET /api/leituras.csv`.
