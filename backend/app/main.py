"""API de ingestão do processo de pintura de blocos de madeira.

O ESP32 de monitoramento envia as leituras recebidas via ESP-NOW para
POST /api/leituras (HTTP/HTTPS + JSON). Os dados vão para o SQLite e são
lidos pelo Grafana (plugin frser-sqlite-datasource).
"""
import asyncio
import csv
import io
import json
import logging
import re
import time
from contextlib import asynccontextmanager
from datetime import datetime, timezone

from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import RedirectResponse, StreamingResponse
from pydantic import TypeAdapter, ValidationError

from . import database as db
from .config import settings
from .schemas import IngestResponse, LeituraIn, LeituraOut

log = logging.getLogger("uvicorn.error")

_PAYLOAD = TypeAdapter(LeituraIn | list[LeituraIn])
# printf("%.2f") do ESP32 escreve nan/inf quando o DHT falha -> vira null
_NAN_RE = re.compile(r'(?<=[:\[,])\s*[-+]?(?:nan|inf(?:inity)?)\b', re.IGNORECASE)


async def _limpeza_periodica() -> None:
    """Aplica a política de retenção (equivalente à retention policy do InfluxDB)."""
    while True:
        try:
            apagadas = await asyncio.to_thread(db.apagar_antigas, settings.retention_days)
            if apagadas:
                log.info("Retenção: %d leituras antigas removidas", apagadas)
        except Exception:  # noqa: BLE001 - nunca derrubar a API por causa da limpeza
            log.exception("Falha na limpeza de retenção")
        await asyncio.sleep(3600)


@asynccontextmanager
async def lifespan(_: FastAPI):
    db.init_db(keep_open=True)
    log.info("SQLite em %s | retenção=%s dias | auth=%s",
             settings.database_path, settings.retention_days, "on" if settings.api_key else "off")
    task = asyncio.create_task(_limpeza_periodica())
    yield
    task.cancel()
    db.close_db()


app = FastAPI(
    title="API IoT – Processo de Pintura",
    description="Recebe leituras do ESP32 de monitoramento e grava no SQLite para o Grafana.",
    version="1.0.0",
    lifespan=lifespan,
)

# CORS: libera navegadores/painéis web e qualquer cliente HTTP.
# (O ESP32 não é navegador e não faz preflight, mas fica liberado do mesmo jeito.)
_allow_all = "*" in settings.cors_origins
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"] if _allow_all else settings.cors_origins,
    allow_credentials=not _allow_all,  # a spec não permite credenciais com "*"
    allow_methods=["GET", "POST", "OPTIONS"],
    allow_headers=["*"],
    max_age=86400,
)


def verificar_api_key(x_api_key: str | None = Header(None, alias="X-API-Key")) -> None:
    if settings.api_key and x_api_key != settings.api_key:
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, "X-API-Key inválida ou ausente")


def _iso(epoch: int) -> str:
    return datetime.fromtimestamp(epoch, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _to_out(row: dict) -> LeituraOut:
    return LeituraOut(**{**row, "timestamp": _iso(row["ts"]), "alerta": bool(row["alerta"])})


def _parse_periodo(valor: str | None) -> int | None:
    """Aceita epoch (segundos) ou data ISO 8601."""
    if valor is None:
        return None
    if valor.lstrip("-").isdigit():
        return int(valor)
    try:
        dt = datetime.fromisoformat(valor.replace("Z", "+00:00"))
    except ValueError as exc:
        raise HTTPException(422, f"Data inválida: {valor}") from exc
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return int(dt.timestamp())


# --------------------------------------------------------------------------- rotas


@app.get("/", include_in_schema=False)
def raiz():
    return RedirectResponse("/grafana/")


@app.get("/health", tags=["infra"])
def health():
    return {"status": "ok", "hora_servidor": _iso(int(time.time()))}


@app.get("/api/time", tags=["infra"])
def hora_servidor():
    """Hora do servidor — útil para o ESP sincronizar o relógio se não tiver NTP."""
    agora = int(time.time())
    return {"epoch": agora, "iso": _iso(agora)}


@app.post(
    "/api/leituras",
    response_model=IngestResponse,
    status_code=status.HTTP_201_CREATED,
    dependencies=[Depends(verificar_api_key)],
    tags=["leituras"],
    openapi_extra={"requestBody": {"required": True, "content": {"application/json": {
        "schema": _PAYLOAD.json_schema(ref_template="#/components/schemas/{model}"),
        "example": {"nivel_tinta": 75, "temperatura": 24.1, "umidade": 55, "luminosidade": 680,
                    "presenca": 1, "timestamp": "2025-09-02T14:35:00Z"}}}}},
)
async def receber_leituras(request: Request):
    """Recebe **uma** leitura (objeto JSON) ou **um lote** (array JSON).

    Mandar em lote (ex.: a cada 10 s) economiza handshakes TLS no ESP32.
    Valores `nan`/`inf` (DHT com falha) são gravados como `null`.
    """
    texto = (await request.body()).decode("utf-8", errors="replace")
    try:
        dados = json.loads(_NAN_RE.sub("null", texto))
        payload = _PAYLOAD.validate_python(dados)
    except json.JSONDecodeError as exc:
        raise HTTPException(422, f"JSON inválido: {exc.msg} (posição {exc.pos})") from exc
    except ValidationError as exc:
        raise HTTPException(422, exc.errors(include_url=False, include_context=False)) from exc
    leituras = payload if isinstance(payload, list) else [payload]
    if not leituras:
        raise HTTPException(422, "Lista vazia")
    if len(leituras) > 500:
        raise HTTPException(413, "Máximo de 500 leituras por requisição")

    agora = int(time.time())
    rows = [
        {
            **l.model_dump(exclude={"timestamp"}),
            "ts": l.epoch(agora),
            "recebido_em": agora,
            "alerta": int(l.nivel_tinta < settings.nivel_alerta),
        }
        for l in leituras
    ]
    gravadas = await asyncio.to_thread(db.insert_leituras, rows)
    return IngestResponse(recebidas=len(leituras), gravadas=gravadas, alerta=bool(rows[-1]["alerta"]))


@app.get("/api/leituras", response_model=list[LeituraOut], tags=["leituras"])
def listar_leituras(
    inicio: str | None = Query(None, description="Epoch (s) ou ISO 8601, ex.: 2025-09-02T14:00:00Z"),
    fim: str | None = Query(None, description="Epoch (s) ou ISO 8601"),
    device_id: str | None = None,
    limit: int = Query(100, ge=1, le=10000),
):
    """Consulta as leituras salvas (mais recentes primeiro)."""
    rows = db.query_leituras(_parse_periodo(inicio), _parse_periodo(fim), device_id, limit)
    return [_to_out(r) for r in rows]


@app.get("/api/leituras/ultima", response_model=LeituraOut, tags=["leituras"])
def ultima_leitura(device_id: str | None = None):
    rows = db.query_leituras(device_id=device_id, limit=1)
    if not rows:
        raise HTTPException(404, "Nenhuma leitura registrada ainda")
    return _to_out(rows[0])


@app.get("/api/leituras.csv", tags=["leituras"])
def exportar_csv(
    inicio: str | None = None,
    fim: str | None = None,
    device_id: str | None = None,
    limit: int = Query(10000, ge=1, le=100000),
):
    """Exporta as leituras em CSV (abre no Excel/LibreOffice)."""
    rows = db.query_leituras(_parse_periodo(inicio), _parse_periodo(fim), device_id, limit)
    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow(["timestamp", *db.COLUMNS])
    for r in reversed(rows):
        writer.writerow([_iso(r["ts"]), *(r[c] for c in db.COLUMNS)])
    buf.seek(0)
    return StreamingResponse(
        buf, media_type="text/csv",
        headers={"Content-Disposition": "attachment; filename=leituras.csv"},
    )


@app.get("/api/resumo", tags=["leituras"])
def resumo(horas: int = Query(24, ge=1, le=24 * 365)):
    """Indicadores agregados das últimas N horas."""
    dados = db.resumo(int(time.time()) - horas * 3600)
    if dados.get("ultima_leitura"):
        dados["ultima_leitura"] = _iso(dados["ultima_leitura"])
    return {"periodo_horas": horas, **dados}
