# Grafana

## Acesso

| Item | Valor |
|---|---|
| Endereço | [PREENCHER (backend): ex. http://localhost:3000] |
| Fonte de dados | [PREENCHER (backend): InfluxDB, linguagem Flux ou SQL/InfluxQL] |
| Atualização automática | [PREENCHER (backend): ex. a cada 5 s] |

## Painéis

| Painel | Tipo | Campo | Detalhe |
|---|---|---|---|
| Nível do tanque | [PREENCHER] | `nivel_tinta` | [PREENCHER: ex. limiar vermelho abaixo de 20 %] |
| Temperatura e umidade | [PREENCHER] | `temperatura`, `umidade` | [PREENCHER] |
| Luminosidade | [PREENCHER] | `luminosidade` | [PREENCHER] |

## Dashboard exportado

[PREENCHER (backend): exportar em Dashboard settings > JSON Model, salvar como `docs/grafana-dashboard.json` e colocar um print em `docs/img/grafana.png`]

## Como importar

1. No Grafana, crie a fonte de dados InfluxDB com os dados de [banco-de-dados.md](banco-de-dados.md).
2. Dashboards > New > Import > Upload JSON file e escolha `docs/grafana-dashboard.json`.
3. Se algum painel ficar sem dados, abra o painel e selecione a fonte de dados criada no passo 1.
