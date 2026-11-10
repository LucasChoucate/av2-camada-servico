"""Configuração lida de variáveis de ambiente (12-factor, pronto para o Render)."""
import os
from dataclasses import dataclass, field


def _list(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


@dataclass(frozen=True)
class Settings:
    # Caminho do arquivo SQLite (compartilhado com o Grafana)
    database_path: str = field(default_factory=lambda: os.getenv("DATABASE_PATH", "./data/pintura.db"))
    # Chave exigida no header X-API-Key para gravar leituras. Vazio = sem autenticação.
    api_key: str = field(default_factory=lambda: os.getenv("API_KEY", ""))
    # Retenção dos dados em dias (0 = guardar para sempre)
    retention_days: int = field(default_factory=lambda: int(os.getenv("RETENTION_DAYS", "30")))
    # Origens liberadas no CORS, separadas por vírgula. "*" libera todas.
    cors_origins: list[str] = field(default_factory=lambda: _list(os.getenv("CORS_ORIGINS", "*")))
    # Limite de nível de tinta para considerar alerta (%)
    nivel_alerta: float = field(default_factory=lambda: float(os.getenv("NIVEL_ALERTA", "20")))


settings = Settings()
