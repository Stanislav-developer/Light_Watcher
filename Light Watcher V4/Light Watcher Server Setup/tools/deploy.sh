#!/bin/bash
set -euo pipefail

SERVER_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REMOTE_DIR="light-watcher"

TARGET="${1:-}"
PORT="${2:-}"
if [ -z "$TARGET" ]; then
  read -r -p "IP сервера (VPS): " IP
  read -r -p "Логін на сервері [root]: " LOGIN
  LOGIN="${LOGIN:-root}"
  TARGET="$LOGIN@$IP"
elif [[ "$TARGET" == *@* ]]; then
  IP="${TARGET#*@}"
else
  IP="$TARGET"
  read -r -p "Логін на сервері [root]: " LOGIN
  LOGIN="${LOGIN:-root}"
  TARGET="$LOGIN@$IP"
fi
if [ -z "$PORT" ]; then
  read -r -p "Порт сайту й API [8000]: " PORT
  PORT="${PORT:-8000}"
fi
if ! [[ "$IP" =~ ^[A-Za-z0-9.-]+$ ]]; then
  echo "❌ Некоректна адреса сервера: $IP"
  exit 1
fi
if ! [[ "$PORT" =~ ^[0-9]{1,5}$ ]] || [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
  echo "❌ Порт — число від 1 до 65535."
  exit 1
fi

WORK="$(mktemp -d /tmp/lw.XXXXXX)"
SOCK="$WORK/ssh"
cleanup() {
  ssh -S "$SOCK" -O exit "$TARGET" >/dev/null 2>&1 || true
  rm -rf "$WORK"
}
trap cleanup EXIT

echo "→ Підключаюся до $TARGET. Пароль від сервера (якщо попросить) вводите лише тут, один раз."
ssh -o ControlMaster=yes -o ControlPersist=15m -o ServerAliveInterval=20 -S "$SOCK" -fN "$TARGET"
rssh() { ssh -S "$SOCK" "$TARGET" "$@"; }

HAS_ENV="no"
if rssh "test -f ~/$REMOTE_DIR/.env"; then
  HAS_ENV="yes"
fi

TOKEN=""
OWNER=""
if [ -n "${LW_ENV_FILE:-}" ]; then
  TOKEN="$(sed -n 's/^LW_BOT_TOKEN=//p' "$LW_ENV_FILE" | head -1)"
  OWNER="$(sed -n 's/^LW_OWNER_ID=//p' "$LW_ENV_FILE" | head -1)"
  echo "→ Налаштування (токен і ID) взято зі старого сервера."
else
  echo
  if [ "$HAS_ENV" = "yes" ]; then
    echo "На сервері вже є налаштування. Enter — лишити як є."
  fi
  read -r -s -p "Токен бота (той самий, що на пристрої; не показується): " TOKEN
  echo
  if [ -n "$TOKEN" ] || [ "$HAS_ENV" = "no" ]; then
    read -r -p "Ваш Telegram ID (число): " OWNER
  fi
fi

if [ -z "$TOKEN" ] && [ "$HAS_ENV" = "no" ]; then
  echo "❌ Потрібен токен бота: без нього сервер не впізнає пристрій."
  exit 1
fi
if [ -n "$TOKEN" ]; then
  if ! [[ "$TOKEN" =~ ^[0-9]{6,12}:[A-Za-z0-9_-]{30,}$ ]]; then
    echo "❌ Токен має вигляд 123456789:AA… (як видав @BotFather)."
    exit 1
  fi
  if ! [[ "$OWNER" =~ ^[0-9]{1,20}$ ]]; then
    echo "❌ Telegram ID — додатне число (ваш особистий ID, його показує @userinfobot)."
    exit 1
  fi
  (umask 077; printf 'LW_BOT_TOKEN=%s\nLW_OWNER_ID=%s\nLW_TS_WINDOW_SEC=300\nLW_TZ=Europe/Kyiv\n' "$TOKEN" "$OWNER" > "$WORK/env")
fi
unset TOKEN

echo "→ Копіюю сервер у ~/${REMOTE_DIR}…"
COPYFILE_DISABLE=1 tar -czf - --no-xattrs -C "$SERVER_DIR" \
  --exclude .venv --exclude data --exclude tests --exclude __pycache__ --exclude .pytest_cache \
  --exclude .env --exclude '*.sqlite3' --exclude '*.pyc' --exclude .git --exclude .gitignore --exclude '*.md' --exclude .DS_Store . \
  | rssh "mkdir -p ~/$REMOTE_DIR && tar -xzf - -C ~/$REMOTE_DIR 2>/dev/null"
if [ -f "$WORK/env" ]; then
  rssh "umask 077; cat > ~/$REMOTE_DIR/.env" < "$WORK/env"
  rm -f "$WORK/env"
fi

echo "→ Встановлюю й запускаю на сервері…"
ssh -t -S "$SOCK" "$TARGET" "sh ~/$REMOTE_DIR/tools/remote_setup.sh $PORT"

echo "→ Перевіряю доступ ззовні: http://$IP:$PORT/healthz"
if curl -fsS --max-time 8 "http://$IP:$PORT/healthz" >/dev/null 2>&1; then
  ADDR="$IP"
  [ "$PORT" = "8000" ] || ADDR="$IP:$PORT"
  echo
  echo "✅ Готово. Сервер працює й доступний з інтернету."
  echo "Тепер у боті: ⚙️ Налаштування → 🖥 Сервер → 🌍 Підключити сервер → надішліть: $ADDR"
  echo "Сайт після «🪞 Увімкнути дзеркало»: http://$IP:$PORT/"
else
  echo
  echo "⚠️ На сервері все запущено, але ззовні порт $PORT не відповідає."
  echo "Відкрийте порт $PORT/tcp у фаєрволі в панелі хостингу (Security group / Firewall) і перевірте:"
  echo "http://$IP:$PORT/healthz — має бути {\"ok\":1,\"api\":1}"
  exit 2
fi
