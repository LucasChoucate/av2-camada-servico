"""Modelos de entrada/saída da API (validação dos campos pedidos no edital)."""
from datetime import datetime, timezone

from pydantic import BaseModel, ConfigDict, Field, field_validator


class LeituraIn(BaseModel):
    """Pacote enviado pelo ESP32 de monitoramento.

    Exemplo:
    {"nivel_tinta": 75, "temperatura": 24.1, "umidade": 55, "luminosidade": 680,
     "presenca": 1, "timestamp": "2025-09-02T14:35:00Z"}
    """

    model_config = ConfigDict(extra="ignore")

    nivel_tinta: float = Field(..., ge=0, le=100, description="Nível do tanque (%)")
    temperatura: float | None = Field(None, ge=-40, le=85, description="Temperatura (°C)")
    umidade: float | None = Field(None, ge=0, le=100, description="Umidade relativa (%)")
    luminosidade: int = Field(..., ge=0, le=4095, description="Leitura analógica do LDR")
    presenca: int | None = Field(None, ge=0, le=1, description="0 = ausente, 1 = detectada (opcional: sem PIR)")
    timestamp: datetime | None = Field(
        None, description="Momento da leitura em ISO 8601. Se ausente/inválido, usa a hora do servidor."
    )
    device_id: str = Field("chao-fabrica-01", max_length=64)
    uptime_emissor_ms: int | None = Field(None, ge=0, description="millis() do ESP32 chão de fábrica")

    @field_validator("presenca", mode="before")
    @classmethod
    def _bool_para_int(cls, v):
        if isinstance(v, bool):
            return int(v)
        return v

    @field_validator("timestamp", mode="before")
    @classmethod
    def _timestamp_vazio(cls, v):
        # ESP sem NTP costuma mandar "" ou "1970-01-01T..." — tratamos como ausente
        if v in ("", None, 0):
            return None
        return v

    def epoch(self, agora: int) -> int:
        if self.timestamp is None:
            return agora
        ts = self.timestamp
        if ts.tzinfo is None:
            ts = ts.replace(tzinfo=timezone.utc)
        epoch = int(ts.timestamp())
        # Relógio não sincronizado (antes de 2020) ou muito no futuro -> hora do servidor
        if epoch < 1_577_836_800 or epoch > agora + 300:
            return agora
        return epoch


class LeituraOut(BaseModel):
    id: int
    ts: int
    timestamp: str
    recebido_em: int
    device_id: str
    nivel_tinta: float
    temperatura: float | None
    umidade: float | None
    luminosidade: int
    presenca: int | None
    uptime_emissor_ms: int | None
    alerta: bool


class IngestResponse(BaseModel):
    recebidas: int
    gravadas: int
    alerta: bool
