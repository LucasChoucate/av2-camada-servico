#!/usr/bin/env python3
"""Simula o ESP32 de monitoramento enviando leituras para a API.

Envia o mesmo JSON que firmware/monitoramento/monitoramento.ino monta.

Usa só a biblioteca padrão (não precisa instalar nada).

Exemplos:
    python scripts/simular_esp.py --url http://localhost:8080
    python scripts/simular_esp.py --url https://SEU-APP.onrender.com --lote 5
    python scripts/simular_esp.py --url http://localhost:8080 --historico 3600   # 1 h de dados passados
"""
import argparse
import json
import math
import random
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone


class Tanque:
    def __init__(self):
        self.nivel = 90.0
        self.inicio = time.time()

    def ler(self, t: float) -> dict:
        # Consome tinta aos poucos e "reabastece" quando chega a 8%
        self.nivel -= random.uniform(0.05, 0.4)
        if self.nivel < 8:
            self.nivel = 95.0
        return {
            "nivel_tinta": int(self.nivel),
            "temperatura": round(24 + 2 * math.sin(t / 600) + random.uniform(-0.3, 0.3), 1),
            "umidade": round(55 + 8 * math.sin(t / 900 + 1) + random.uniform(-1, 1), 1),
            "luminosidade": int(max(0, min(4095, 680 + 300 * math.sin(t / 300) + random.uniform(-40, 40)))),
            "timestamp": datetime.fromtimestamp(t, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "uptime_emissor_ms": int((t - self.inicio) * 1000) % 2**32,
        }


def enviar(url: str, api_key: str, payload) -> str:
    req = urllib.request.Request(
        url.rstrip("/") + "/api/leituras",
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", **({"X-API-Key": api_key} if api_key else {})},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return f"{r.status} {r.read().decode()}"
    except urllib.error.HTTPError as e:
        return f"{e.code} {e.read().decode()}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:8080")
    ap.add_argument("--api-key", default="", help="só se a API_KEY estiver definida no servidor")
    ap.add_argument("--intervalo", type=float, default=2.0, help="segundos entre leituras")
    ap.add_argument("--lote", type=int, default=1, help="leituras por requisição")
    ap.add_argument("--historico", type=int, default=0, help="gera N segundos de dados passados e sai")
    args = ap.parse_args()

    tanque = Tanque()

    if args.historico:
        agora = time.time()
        leituras = [tanque.ler(t) for t in range(int(agora - args.historico), int(agora), int(args.intervalo))]
        for i in range(0, len(leituras), 500):
            print(enviar(args.url, args.api_key, leituras[i:i + 500]))
        return

    buffer = []
    while True:
        buffer.append(tanque.ler(time.time()))
        if len(buffer) >= args.lote:
            payload = buffer[0] if len(buffer) == 1 else buffer
            print(datetime.now().strftime("%H:%M:%S"), enviar(args.url, args.api_key, payload))
            buffer = []
        time.sleep(args.intervalo)


if __name__ == "__main__":
    main()
