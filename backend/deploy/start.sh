#!/usr/bin/env bash
# Sobe API (uvicorn) + Grafana + nginx no mesmo container.
# Se qualquer um cair, o container encerra e o Render reinicia o serviço.
set -euo pipefail

export PORT="${PORT:-10000}"
export DATABASE_PATH="${DATABASE_PATH:-/data/pintura.db}"
PUBLIC_URL="${PUBLIC_URL:-${RENDER_EXTERNAL_URL:-http://localhost:${PORT}}}"

mkdir -p "$(dirname "$DATABASE_PATH")" "$GF_PATHS_DATA" "$GF_PATHS_LOGS"

# Cria o schema antes do Grafana tentar abrir o arquivo
/opt/venv/bin/python -c "from app.database import init_db; init_db()"

# ---------- Grafana (somente em localhost; exposto via nginx em /grafana)
export GF_SERVER_HTTP_ADDR=127.0.0.1
export GF_SERVER_HTTP_PORT=3000
export GF_SERVER_ROOT_URL="${PUBLIC_URL%/}/grafana/"
export GF_SERVER_SERVE_FROM_SUB_PATH=true
export GF_SECURITY_ADMIN_USER="${GF_SECURITY_ADMIN_USER:-admin}"
export GF_SECURITY_ADMIN_PASSWORD="${GF_SECURITY_ADMIN_PASSWORD:-admin}"
export GF_AUTH_ANONYMOUS_ENABLED="${GF_AUTH_ANONYMOUS_ENABLED:-true}"
export GF_AUTH_ANONYMOUS_ORG_ROLE=Viewer
export GF_USERS_ALLOW_SIGN_UP=false
export GF_DASHBOARDS_DEFAULT_HOME_DASHBOARD_PATH=/etc/grafana/dashboards/pintura.json
export GF_ANALYTICS_REPORTING_ENABLED=false
export GF_ANALYTICS_CHECK_FOR_UPDATES=false
export GF_ANALYTICS_CHECK_FOR_PLUGIN_UPDATES=false
export GF_NEWS_NEWS_FEED_ENABLED=false
export GF_LOG_LEVEL="${GF_LOG_LEVEL:-warn}"

grafana server \
  --homepath="$GF_PATHS_HOME" \
  --config="$GF_PATHS_CONFIG" \
  --packaging=docker \
  cfg:default.log.mode=console \
  cfg:default.paths.data="$GF_PATHS_DATA" \
  cfg:default.paths.logs="$GF_PATHS_LOGS" \
  cfg:default.paths.plugins="$GF_PATHS_PLUGINS" \
  cfg:default.paths.provisioning="$GF_PATHS_PROVISIONING" &

# ---------- API
/opt/venv/bin/uvicorn app.main:app \
  --host 127.0.0.1 --port 8000 \
  --proxy-headers --forwarded-allow-ips='*' \
  --no-access-log &

# ---------- nginx (porta pública)
envsubst '${PORT}' < /app/deploy/nginx.conf.template > /tmp/nginx.conf
nginx -c /tmp/nginx.conf -g 'daemon off;' &

echo "Serviço no ar: ${PUBLIC_URL}  (API: /docs  |  Grafana: /grafana/)"
wait -n
echo "Um dos processos encerrou — finalizando container." >&2
exit 1
