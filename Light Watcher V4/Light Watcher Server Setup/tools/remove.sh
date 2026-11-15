#!/bin/bash
set -euo pipefail

REMOTE_DIR="light-watcher"
TARGET="${1:-}"
if [ -z "$TARGET" ]; then
  read -r -p "IP сервера (VPS), з якого видалити Light Watcher: " IP
  read -r -p "Логін на сервері [root]: " LOGIN
  TARGET="${LOGIN:-root}@$IP"
elif [[ "$TARGET" != *@* ]]; then
  read -r -p "Логін на сервері [root]: " LOGIN
  TARGET="${LOGIN:-root}@$TARGET"
fi
IP="${TARGET#*@}"

WORK="$(mktemp -d /tmp/lw.XXXXXX)"
SOCK="$WORK/ssh"
cleanup() {
  ssh -S "$SOCK" -O exit "$TARGET" >/dev/null 2>&1 || true
  rm -rf "$WORK"
}
trap cleanup EXIT

echo "→ Підключаюся до $TARGET. Пароль від сервера (якщо попросить) вводите лише тут."
ssh -o ControlMaster=yes -o ControlPersist=15m -S "$SOCK" -fN "$TARGET"
rssh() { ssh -S "$SOCK" "$TARGET" "$@"; }
SUDO_CMD='S=""; [ "$(id -u)" -eq 0 ] || S=sudo;'

if ! rssh "test -d ~/$REMOTE_DIR"; then
  echo "На $IP немає ~/$REMOTE_DIR — Light Watcher тут не встановлено скриптом."
  exit 1
fi

echo
echo "Буде видалено з $IP: контейнер light-watcher, його образ і папку ~/$REMOTE_DIR (з базою історії)."
echo "Спершу в боті натисніть ⚙️ → 🖥 Сервер → 🗑 Відв'язати (або змініть адресу на інший сервер), інакше пристрій і далі стукатиме сюди."
read -r -p "Зберегти історію (базу) на Робочий стіл перед видаленням? [Т/н] " KEEP
read -r -p "Щоб видалити, введіть слово  видалити : " OK
if [ "$OK" != "видалити" ]; then
  echo "Скасовано, нічого не змінено."
  exit 0
fi

ssh -t -S "$SOCK" "$TARGET" "$SUDO_CMD \$S docker stop light-watcher >/dev/null 2>&1 || true"
case "${KEEP:-т}" in
  н|Н|n|N|ні|Ні) ;;
  *)
    OUTDIR="$HOME/Desktop"
    [ -d "$OUTDIR" ] || OUTDIR="$HOME"
    OUT="$OUTDIR/lw-backup-$IP-$(date +%Y%m%d-%H%M).sqlite3"
    if rssh "test -f ~/$REMOTE_DIR/data/lw.sqlite3"; then
      if ! rssh "cat ~/$REMOTE_DIR/data/lw.sqlite3" > "$OUT"; then
        ssh -t -S "$SOCK" "$TARGET" "$SUDO_CMD \$S docker start light-watcher >/dev/null 2>&1 || true"
        echo "❌ Історію не вдалося зберегти — нічого не видалено, сервер запущено знову."
        exit 1
      fi
      echo "✅ Історію збережено: $OUT"
    else
      echo "Бази на сервері немає — зберігати нічого."
    fi
    ;;
esac

ssh -t -S "$SOCK" "$TARGET" "$SUDO_CMD \$S docker rm -f light-watcher >/dev/null 2>&1; \$S docker rmi light-watcher:latest >/dev/null 2>&1; \$S rm -rf ~/$REMOTE_DIR"
echo "✅ Light Watcher видалено з $IP. Порт у фаєрволі VPS лишився відкритим — закрийте його, якщо він більше не потрібен."
