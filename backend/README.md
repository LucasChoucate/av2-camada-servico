# Backend: API + SQLite + Grafana

Camada de serviço do sistema de monitoramento de pintura de blocos de madeira (SENAI CIMATEC, AV2).
O **ESP32 de monitoramento** recebe os dados do **ESP32 de chão de fábrica** via ESP-NOW e os envia por
**HTTP(S)** para esta API. A API grava as leituras no **SQLite** (no lugar do InfluxDB) e o **Grafana**
mostra os dashboards. Tudo roda em **um único container**, pronto para o Render.

Todos os comandos abaixo são executados **dentro da pasta `backend/`**, exceto onde indicado.

```
ESP32 chão de fábrica ──ESP-NOW──▶ ESP32 monitoramento ──HTTPS POST /api/leituras──┐
                                                                                   ▼
                              ┌──────────────── container (Render) ────────────────────┐
                              │  nginx :$PORT                                          │
                              │   ├── /grafana/*  ──▶ Grafana :3000 ──┐  (somente leitura)│
                              │   └── /*          ──▶ FastAPI :8000 ──┴─▶ SQLite /data/pintura.db │
                              └────────────────────────────────────────────────────────┘
```

## Estrutura

```
../render.yaml         Blueprint do Render (fica na raiz do repositório)
app/                    API FastAPI (config, banco, schemas, rotas)
grafana/provisioning/   fonte de dados SQLite e provedor de dashboards (carregados automaticamente)
grafana/dashboards/     dashboard "Processo de Pintura – Chão de Fábrica"
deploy/                 nginx (proxy reverso) e script de inicialização do container
scripts/simular_esp.py  simulador do ESP32 (para testar sem hardware)
tests/                  testes da API (pytest)
Dockerfile, docker-compose.yml
```

## Rodar localmente

### Opção 1: tudo junto (igual ao Render)

```bash
cp .env.example .env          # ajuste a senha do Grafana
docker compose up --build
```

- Documentação da API (Swagger): http://localhost:8080/docs
- Grafana: http://localhost:8080/grafana/ (acesso anônimo de leitura; admin com `GF_SECURITY_ADMIN_PASSWORD`)

Para gerar dados de teste sem os ESP32:

```bash
python scripts/simular_esp.py --url http://localhost:8080 --historico 3600  # 1 h passada
python scripts/simular_esp.py --url http://localhost:8080                   # ao vivo, a cada 2 s
```

### Opção 2: só a API (sem Grafana)

```bash
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
uvicorn app.main:app --reload --port 8000
```

Testes: `pip install pytest httpx && pytest`

## Deploy no Render

1. Suba este repositório para o GitHub.
2. No Render: **New + → Blueprint**, selecione o repositório e a branch. O `render.yaml` da raiz do
   repositório cria o serviço web Docker a partir de `backend/Dockerfile`.
3. Ao terminar o deploy, abra o serviço e, em **Environment**, veja o valor gerado de
   `GF_SECURITY_ADMIN_PASSWORD` (senha do usuário `admin` do Grafana, se quiser editar dashboards).
4. Endereços, onde `SEU-APP` é o nome do serviço:
   - API: `https://SEU-APP.onrender.com/api/leituras`
   - Swagger: `https://SEU-APP.onrender.com/docs`
   - Grafana: `https://SEU-APP.onrender.com/grafana/`

> **Atenção ao plano gratuito do Render**
> - **O sistema de arquivos é apagado** a cada deploy ou reinício. Para guardar o histórico, use o plano
>   *Starter* e descomente o bloco `disk` no `render.yaml` (montado em `/data`).
> - O serviço **"dorme" após 15 min sem requisições** e leva cerca de 1 minuto para acordar. Com o ESP32
>   enviando dados continuamente, ele fica acordado.
> - O Render só aceita **HTTPS** (requisições HTTP são redirecionadas). O `BACKEND_URL` do firmware precisa
>   começar com `https://` (veja [Compatibilidade com o firmware](#compatibilidade-com-o-firmware-de-monitoramento)).

### Variáveis de ambiente

| Variável | Padrão | Função |
|---|---|---|
| `API_KEY` | vazio | Opcional. Se definida, a API passa a exigir o header `X-API-Key` para gravar leituras |
| `DATABASE_PATH` | `/data/pintura.db` | Arquivo SQLite (compartilhado com o Grafana) |
| `RETENTION_DAYS` | `30` | Apaga leituras mais antigas que N dias (`0` = nunca) |
| `CORS_ORIGINS` | `*` | Origens liberadas no CORS, separadas por vírgula |
| `NIVEL_ALERTA` | `20` | Nível de tinta (%) abaixo do qual a leitura é marcada como alerta |
| `GF_SECURITY_ADMIN_PASSWORD` | `admin` | Senha do usuário `admin` do Grafana |
| `GF_AUTH_ANONYMOUS_ENABLED` | `true` | Dashboard visível sem login (somente leitura) |

## API

### `POST /api/leituras`: envio do ESP32

Header: `Content-Type: application/json`. Não é preciso enviar chave.

Aceita **uma leitura** (objeto) ou **um lote** (array com até 500 itens). Enviar em lote (ex.: 5 leituras a
cada 10 s) economiza handshakes TLS no ESP32.

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

| Campo | Tipo | Regra |
|---|---|---|
| `nivel_tinta` | número | 0–100 (%) **obrigatório** |
| `temperatura` | número/null | -40–85 °C (`null` se o DHT falhar) |
| `umidade` | número/null | 0–100 % (`null` se o DHT falhar) |
| `luminosidade` | inteiro | 0–4095 (ADC) **obrigatório** |
| `presenca` | 0/1/null | opcional. O PIR foi dispensado pelo professor, então o firmware não envia |
| `timestamp` | ISO 8601 | opcional. Se ausente ou com relógio não sincronizado (antes de 2020), usa a hora do servidor |
| `device_id` | texto | opcional, padrão `chao-fabrica-01` |
| `uptime_emissor_ms` | inteiro | opcional, `millis()` do ESP32 chão de fábrica |

Valores `nan`/`inf` (o `printf` do ESP32 gera isso quando o DHT falha) são gravados como `null`.

Respostas: `201 {"recebidas":1,"gravadas":1,"alerta":false}`, `422` (JSON ou campo inválido).

```bash
curl -X POST https://SEU-APP.onrender.com/api/leituras \
  -H "Content-Type: application/json" \
  -d '{"nivel_tinta":75,"temperatura":24.1,"umidade":55,"luminosidade":680,"timestamp":null,"uptime_emissor_ms":123456}'
```

### Compatibilidade com o firmware de monitoramento

`firmware/monitoramento/monitoramento.ino` já envia o formato que a API espera (`POST /api/leituras`,
sem `presenca`, com `timestamp` `null` quando o NTP não sincronizou). Só é preciso ajustar o
`BACKEND_URL` em `firmware/monitoramento/secrets.h`:

| Onde roda o backend | `BACKEND_URL` |
|---|---|
| Render | `https://SEU-APP.onrender.com/api/leituras` |
| Computador na mesma rede (`docker compose up`) | `http://IP-DO-COMPUTADOR:8080/api/leituras` |
| Computador, só a API (`uvicorn ... --host 0.0.0.0 --port 8000`) | `http://IP-DO-COMPUTADOR:8000/api/leituras` |

> No Render, cada envio abre uma conexão HTTPS nova. Junto com a distância até o servidor, isso pode
> passar de 1,5 s, que é o `HTTP_TIMEOUT_MS` atual do firmware. Se o Serial mostrar
> `Backend: falha de conexão (read Timeout)`, aumente esse valor (por exemplo, para 5000).

> Se `http.begin(BACKEND_URL)` não conseguir abrir a conexão HTTPS, troque por um `WiFiClientSecure`
> com `client.setInsecure()` e chame `http.begin(client, BACKEND_URL)`.

### Consultar os dados salvos

| Rota | Descrição |
|---|---|
| `GET /api/leituras?limit=100` | Leituras mais recentes. Filtros: `inicio`, `fim` (epoch ou ISO), `device_id` |
| `GET /api/leituras/ultima` | Última leitura |
| `GET /api/leituras.csv?inicio=2025-09-02T00:00:00Z` | Exporta em CSV (Excel/LibreOffice) |
| `GET /api/resumo?horas=24` | Mínimo, máximo e média do nível, médias ambientais, nº de alertas |
| `GET /api/time` | Hora do servidor (para sincronizar o ESP sem NTP) |
| `GET /health` | Health check |

Direto no banco (terminal do container: `docker compose exec pintura-iot bash`; no Render, aba **Shell**, só nos planos pagos):

```bash
sqlite3 /data/pintura.db "SELECT datetime(ts,'unixepoch') AS hora, nivel_tinta, temperatura, umidade, luminosidade, uptime_emissor_ms FROM leituras ORDER BY ts DESC LIMIT 10;"
```

### CORS

O CORS está liberado para as origens de `CORS_ORIGINS` (padrão `*`), com métodos `GET/POST/OPTIONS` e
qualquer header. O CORS é uma regra dos **navegadores**: o ESP32 não aplica essa
verificação, então ele consegue enviar dados de qualquer forma. A configuração serve para páginas web e
dashboards externos que consumam a API.

## Banco de dados (SQLite)

O SQLite substitui o InfluxDB: é um único arquivo e não precisa de servidor. Ele roda em modo **WAL**, que
permite ao Grafana ler enquanto a API grava. Os conceitos do InfluxDB pedidos no edital ficaram assim:

| InfluxDB (edital) | Neste projeto |
|---|---|
| Bucket | Tabela `leituras` em `/data/pintura.db` |
| Token de acesso | Opcional: definindo `API_KEY`, a escrita exige o header `X-API-Key`. O Grafana acessa o arquivo em modo somente leitura |
| Retenção | `RETENTION_DAYS`: uma tarefa da API apaga as leituras antigas a cada hora |

```sql
CREATE TABLE leituras (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    ts            INTEGER NOT NULL,   -- momento da leitura (epoch UTC, segundos)
    recebido_em   INTEGER NOT NULL,   -- momento em que a API recebeu
    device_id     TEXT    NOT NULL,
    nivel_tinta   REAL    NOT NULL,   -- %
    temperatura   REAL,               -- °C
    umidade       REAL,               -- %
    luminosidade  INTEGER NOT NULL,   -- 0..4095
    presenca      INTEGER,            -- 0/1 (NULL sem PIR)
    uptime_emissor_ms INTEGER,        -- millis() do chão de fábrica
    alerta        INTEGER NOT NULL    -- 1 se nivel_tinta < NIVEL_ALERTA
);
-- índices: (ts) e (device_id, ts)
```

## Grafana

- **Fonte de dados**: plugin [`frser-sqlite-datasource`](https://grafana.com/grafana/plugins/frser-sqlite-datasource/),
  provisionado em `grafana/provisioning/datasources/sqlite.yaml` e apontando para `DATABASE_PATH`.
- **Dashboard** "Processo de Pintura – Chão de Fábrica" (pasta *IoT*, também é a página inicial).
  Atualiza a cada **5 s**:
  - Indicadores atuais: medidor do nível do tanque (vermelho abaixo de 20%), estado do sistema
    ("Operação normal" ou "ALERTA – verificar tanque"), temperatura, umidade, luminosidade e há quantos
    segundos chegou a última leitura (verde até 10 s, vermelho a partir de 30 s).
  - Gráfico do nível do tanque, com a faixa crítica (< 20%) destacada.
  - Gráficos de temperatura e umidade e de luminosidade.
  - Gráfico de leituras recebidas por minuto (~30 com envio a cada 2 s) e tabela das últimas leituras.
- Presença não aparece no dashboard porque o PIR foi dispensado. A API ainda aceita o campo, caso ele
  volte a ser usado.
- Descrição detalhada dos painéis e consultas: [../docs/grafana.md](../docs/grafana.md).
- O dashboard é provisionado a partir de `grafana/dashboards/pintura.json`, e o Grafana não deixa salvar
  alterações pela interface. Para alterá-lo, edite no Grafana, copie o JSON em
  **Dashboard settings → JSON Model** e substitua esse arquivo.
