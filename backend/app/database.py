"""Camada de acesso ao SQLite (substitui o InfluxDB do edital).

Tabela única `leituras`, com `ts` em segundos Unix (UTC) — formato que o
plugin SQLite do Grafana entende como coluna de tempo.
"""
import sqlite3
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

from .config import settings

SCHEMA = """
CREATE TABLE IF NOT EXISTS leituras (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    ts            INTEGER NOT NULL,              -- momento da leitura (epoch UTC, segundos)
    recebido_em   INTEGER NOT NULL,              -- momento em que a API recebeu (epoch UTC)
    device_id     TEXT    NOT NULL DEFAULT 'chao-fabrica-01',
    nivel_tinta   REAL    NOT NULL,              -- %
    temperatura   REAL,                          -- °C (NULL se o DHT falhar)
    umidade       REAL,                          -- %  (NULL se o DHT falhar)
    luminosidade  INTEGER NOT NULL,              -- 0..4095 (ADC do ESP32)
    presenca      INTEGER,                       -- 0 = ausente, 1 = detectada (NULL sem PIR)
    uptime_emissor_ms INTEGER,                   -- millis() do ESP32 chão de fábrica
    alerta        INTEGER NOT NULL               -- 1 se nível < limite
);
CREATE INDEX IF NOT EXISTS idx_leituras_ts ON leituras(ts);
CREATE INDEX IF NOT EXISTS idx_leituras_device_ts ON leituras(device_id, ts);
"""

COLUMNS = (
    "id", "ts", "recebido_em", "device_id", "nivel_tinta", "temperatura",
    "umidade", "luminosidade", "presenca", "uptime_emissor_ms", "alerta",
)


def _connect() -> sqlite3.Connection:
    conn = sqlite3.connect(settings.database_path, timeout=10)
    conn.row_factory = sqlite3.Row
    # WAL permite que o Grafana leia enquanto a API escreve
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    conn.execute("PRAGMA busy_timeout=5000")
    return conn


@contextmanager
def get_conn() -> Iterator[sqlite3.Connection]:
    conn = _connect()
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


_keeper: sqlite3.Connection | None = None


def init_db(keep_open: bool = False) -> None:
    """Cria o schema. Com keep_open, mantém uma conexão aberta durante a vida da API:
    assim o índice WAL (-shm) fica inicializado e os leitores do Grafana não
    disputam a recuperação do WAL a cada abertura (evita 'database is locked')."""
    global _keeper
    Path(settings.database_path).parent.mkdir(parents=True, exist_ok=True)
    with get_conn() as conn:
        conn.executescript(SCHEMA)
    if keep_open and _keeper is None:
        _keeper = _connect()


def close_db() -> None:
    global _keeper
    if _keeper is not None:
        _keeper.close()
        _keeper = None


def insert_leituras(rows: list[dict]) -> int:
    sql = (
        "INSERT INTO leituras (ts, recebido_em, device_id, nivel_tinta, temperatura, "
        "umidade, luminosidade, presenca, uptime_emissor_ms, alerta) VALUES "
        "(:ts, :recebido_em, :device_id, :nivel_tinta, :temperatura, :umidade, "
        ":luminosidade, :presenca, :uptime_emissor_ms, :alerta)"
    )
    with get_conn() as conn:
        conn.executemany(sql, rows)
    return len(rows)


def query_leituras(
    inicio: int | None = None,
    fim: int | None = None,
    device_id: str | None = None,
    limit: int = 100,
) -> list[dict]:
    where, params = [], {}
    if inicio is not None:
        where.append("ts >= :inicio")
        params["inicio"] = inicio
    if fim is not None:
        where.append("ts <= :fim")
        params["fim"] = fim
    if device_id:
        where.append("device_id = :device_id")
        params["device_id"] = device_id
    sql = f"SELECT {', '.join(COLUMNS)} FROM leituras"
    if where:
        sql += " WHERE " + " AND ".join(where)
    sql += " ORDER BY ts DESC, id DESC LIMIT :limit"
    params["limit"] = limit
    with get_conn() as conn:
        return [dict(r) for r in conn.execute(sql, params).fetchall()]


def resumo(desde: int) -> dict:
    sql = """
        SELECT COUNT(*)              AS total_leituras,
               MIN(nivel_tinta)      AS nivel_min,
               MAX(nivel_tinta)      AS nivel_max,
               AVG(nivel_tinta)      AS nivel_medio,
               AVG(temperatura)      AS temperatura_media,
               AVG(umidade)          AS umidade_media,
               AVG(luminosidade)     AS luminosidade_media,
               SUM(presenca)         AS leituras_com_presenca,
               SUM(alerta)           AS leituras_em_alerta,
               MAX(ts)               AS ultima_leitura
        FROM leituras WHERE ts >= ?
    """
    with get_conn() as conn:
        return dict(conn.execute(sql, (desde,)).fetchone())


def apagar_antigas(dias: int) -> int:
    if dias <= 0:
        return 0
    limite = int(time.time()) - dias * 86400
    with get_conn() as conn:
        return conn.execute("DELETE FROM leituras WHERE ts < ?", (limite,)).rowcount
