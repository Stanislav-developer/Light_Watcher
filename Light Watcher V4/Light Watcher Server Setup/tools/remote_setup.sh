#!/bin/sh
set -e
cd "$(dirname "$0")/.."
PORT="${1:-8000}"
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
  echo "Потрібні права адміністратора: sudo може запитати пароль користувача на сервері."
fi

if ! command -v docker >/dev/null 2>&1; then
  if command -v apt-get >/dev/null 2>&1; then
    echo "Встановлюю Docker…"
    $SUDO apt-get update -qq
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq docker.io >/dev/null
  elif command -v dnf >/dev/null 2>&1; then
    echo "Встановлюю Docker…"
    $SUDO dnf install -y -q docker
  else
    echo "Не знаю, як поставити Docker на цій системі. Встановіть його вручну й запустіть скрипт ще раз."
    exit 1
  fi
fi
$SUDO systemctl enable --now docker >/dev/null 2>&1 || $SUDO service docker start >/dev/null 2>&1 || true

mkdir -p data
chmod 600 .env

echo "Збираю образ сервера…"
$SUDO docker build -q -t light-watcher:latest . >/dev/null
$SUDO docker rm -f light-watcher >/dev/null 2>&1 || true
$SUDO docker run -d --name light-watcher --restart unless-stopped --env-file .env \
  -e LW_DB_PATH=/data/lw.sqlite3 -v "$PWD/data:/data" -p "$PORT:8000" light-watcher:latest >/dev/null

if command -v ufw >/dev/null 2>&1 && $SUDO ufw status 2>/dev/null | grep -q "Status: active"; then
  $SUDO ufw allow "$PORT/tcp" >/dev/null
  echo "Фаєрвол ufw: порт $PORT відкрито."
elif command -v firewall-cmd >/dev/null 2>&1 && $SUDO firewall-cmd --state >/dev/null 2>&1; then
  $SUDO firewall-cmd --quiet --permanent --add-port="$PORT/tcp" && $SUDO firewall-cmd --quiet --reload
  echo "Фаєрвол firewalld: порт $PORT відкрито."
fi

i=0
while [ $i -lt 20 ]; do
  if $SUDO docker exec light-watcher python -c "import urllib.request;print(urllib.request.urlopen('http://127.0.0.1:8000/healthz',timeout=2).read().decode())" 2>/dev/null; then
    echo "Light Watcher запущено (порт $PORT)."
    exit 0
  fi
  i=$((i + 1))
  sleep 1
done
echo "Сервер не відповів за 20 с. Журнал:"
$SUDO docker logs --tail 30 light-watcher
exit 1
