# Imagem única: Grafana + API FastAPI + nginx (proxy reverso) + SQLite.
# Base oficial do Grafana (Ubuntu 24.04), onde adicionamos Python e nginx.
FROM grafana/grafana:12.4.14-ubuntu

USER root

RUN apt-get update \
 && apt-get install -y --no-install-recommends python3 python3-venv nginx gettext-base sqlite3 \
 && rm -rf /var/lib/apt/lists/* \
 && rm -f /etc/nginx/sites-enabled/default

# Plugin SQLite do Grafana instalado no build (fora de /var/lib/grafana)
ENV GF_PATHS_PLUGINS=/opt/grafana-plugins
RUN mkdir -p "$GF_PATHS_PLUGINS" \
 && grafana cli --pluginsDir "$GF_PATHS_PLUGINS" plugins install frser-sqlite-datasource 4.0.6

# Dependências Python em venv (Ubuntu 24.04 bloqueia pip no sistema)
WORKDIR /app
COPY requirements.txt .
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir -r requirements.txt

# Código da API, provisionamento do Grafana e scripts de deploy
COPY app ./app
COPY deploy ./deploy
COPY grafana/provisioning /etc/grafana/provisioning
COPY grafana/dashboards /etc/grafana/dashboards

ENV PORT=10000 \
    DATABASE_PATH=/data/pintura.db \
    PYTHONUNBUFFERED=1

EXPOSE 10000
ENTRYPOINT []
CMD ["/app/deploy/start.sh"]
