import importlib
import os
import time

import pytest
from fastapi.testclient import TestClient


@pytest.fixture()
def client(tmp_path, monkeypatch):
    monkeypatch.setenv("DATABASE_PATH", str(tmp_path / "test.db"))
    monkeypatch.setenv("API_KEY", "segredo")
    import app.config, app.database, app.main  # noqa: E401
    for mod in (app.config, app.database, app.main):
        importlib.reload(mod)
    with TestClient(app.main.app) as c:
        yield c


LEITURA = {"nivel_tinta": 75, "temperatura": 24.1, "umidade": 55, "luminosidade": 680, "presenca": 1}
AUTH = {"X-API-Key": "segredo"}


def test_exige_api_key(client):
    assert client.post("/api/leituras", json=LEITURA).status_code == 401


def test_grava_e_consulta_unica(client):
    r = client.post("/api/leituras", json={**LEITURA, "timestamp": "2025-09-02T14:35:00Z"}, headers=AUTH)
    assert r.status_code == 201, r.text
    assert r.json() == {"recebidas": 1, "gravadas": 1, "alerta": False}
    ultima = client.get("/api/leituras/ultima").json()
    assert ultima["timestamp"] == "2025-09-02T14:35:00Z"
    assert ultima["presenca"] == 1


def test_lote_com_alerta_e_dht_nulo(client):
    lote = [LEITURA, {**LEITURA, "nivel_tinta": 10, "temperatura": None, "umidade": None}]
    r = client.post("/api/leituras", json=lote, headers=AUTH)
    assert r.status_code == 201
    assert r.json()["gravadas"] == 2 and r.json()["alerta"] is True
    assert len(client.get("/api/leituras").json()) == 2


def test_timestamp_nao_sincronizado_usa_hora_servidor(client):
    client.post("/api/leituras", json={**LEITURA, "timestamp": "1970-01-01T00:00:12Z"}, headers=AUTH)
    assert abs(client.get("/api/leituras/ultima").json()["ts"] - time.time()) < 5


def test_validacao(client):
    r = client.post("/api/leituras", json={**LEITURA, "nivel_tinta": 150}, headers=AUTH)
    assert r.status_code == 422


def test_cors_preflight(client):
    r = client.options(
        "/api/leituras",
        headers={"Origin": "http://exemplo.com", "Access-Control-Request-Method": "POST",
                 "Access-Control-Request-Headers": "content-type,x-api-key"},
    )
    assert r.status_code == 200
    assert r.headers["access-control-allow-origin"] == "*"


def test_csv_e_resumo(client):
    client.post("/api/leituras", json=LEITURA, headers=AUTH)
    assert client.get("/api/leituras.csv").text.startswith("timestamp,id,ts")
    assert client.get("/api/resumo").json()["total_leituras"] == 1


# Payload exatamente como firmware/monitoramento/monitoramento.ino monta (snprintf)
FIRMWARE = ('{"nivel_tinta":75,"temperatura":24.10,"umidade":55.00,'
            '"luminosidade":680,"timestamp":"2025-09-02T14:35:00Z","uptime_emissor_ms":123456}')


def test_payload_do_firmware(client):
    r = client.post("/api/leituras", content=FIRMWARE, headers={**AUTH, "Content-Type": "application/json"})
    assert r.status_code == 201, r.text
    u = client.get("/api/leituras/ultima").json()
    assert u["presenca"] is None and u["uptime_emissor_ms"] == 123456
    assert u["timestamp"] == "2025-09-02T14:35:00Z"


def test_firmware_sem_ntp_e_dht_nan(client):
    corpo = ('{"nivel_tinta":12,"temperatura":nan,"umidade":-nan,'
             '"luminosidade":680,"timestamp":null,"uptime_emissor_ms":99}')
    r = client.post("/api/leituras", content=corpo, headers={**AUTH, "Content-Type": "application/json"})
    assert r.status_code == 201, r.text
    u = client.get("/api/leituras/ultima").json()
    assert u["temperatura"] is None and u["umidade"] is None and u["alerta"] is True
    assert abs(u["ts"] - time.time()) < 5


def test_json_invalido(client):
    r = client.post("/api/leituras", content="{quebrado", headers={**AUTH, "Content-Type": "application/json"})
    assert r.status_code == 422
