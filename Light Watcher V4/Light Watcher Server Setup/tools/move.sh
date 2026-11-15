#!/bin/bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REMOTE_DIR="light-watcher"
OLD="${1:-}"
NEW="${2:-}"
PORT="${3:-8000}"
if [ -z "$OLD" ] || [ -z "$NEW" ]; then
  echo "Як запускати:  tools/move.sh root@СТАРИЙ_IP root@НОВИЙ_IP [порт]"
  exit 1
fi
[[ "$OLD" == *@* ]] || OLD="root@$OLD"
[[ "$NEW" == *@* ]] || NEW="root@$NEW"
NEW_IP="${NEW#*@}"

WORK="$(mktemp -d /tmp/lw.XXXXXX)"
SO="$WORK/old"
SN="$WORK/new"
cleanup() {
  ssh -S "$SO" -O exit "$OLD" >/dev/null 2>&1 || true
  ssh -S "$SN" -O exit "$NEW" >/dev/null 2>&1 || true
  rm -rf "$WORK"
}
trap cleanup EXIT
SUDO_CMD='S=""; [ "$(id -u)" -eq 0 ] || S=sudo;'

echo "Переїзд Light Watcher: $OLD → $NEW (порт $PORT)."
echo "1) новий сервер налаштовується тими самими токеном і ID; 2) старий зупиняється, історія переноситься; 3) ви міняєте адресу в боті."
echo
echo "→ Старий сервер $OLD. Пароль від нього (якщо попросить) вводите тут."
ssh -o ControlMaster=yes -o ControlPersist=30m -S "$SO" -fN "$OLD"
if ! ssh -S "$SO" "$OLD" "test -f ~/$REMOTE_DIR/.env"; then
  echo "❌ На $OLD немає ~/$REMOTE_DIR/.env — це не сервер, налаштований скриптом."
  exit 1
fi
(umask 077; ssh -S "$SO" "$OLD" "cat ~/$REMOTE_DIR/.env" > "$WORK/env")

echo
echo "→ Налаштовую новий сервер ${NEW}…"
LW_ENV_FILE="$WORK/env" "$HERE/deploy.sh" "$NEW" "$PORT" || {
  echo "❌ Новий сервер не налаштувався. Старий працює далі, нічого не змінено."
  exit 1
}
rm -f "$WORK/env"

echo
echo "→ Зупиняю старий сервер і забираю історію…"
ssh -t -S "$SO" "$OLD" "$SUDO_CMD \$S docker stop light-watcher >/dev/null"
OUTDIR="$HOME/Desktop"
[ -d "$OUTDIR" ] || OUTDIR="$HOME"
BACKUP="$OUTDIR/lw-move-$(date +%Y%m%d-%H%M).sqlite3"
restart_old() {
  ssh -t -S "$SO" "$OLD" "$SUDO_CMD \$S docker start light-watcher >/dev/null" || true
  echo "❌ Історію не перенесено, старий сервер запущено знову. Новий налаштовано, але без старої історії."
  exit 1
}
if ssh -S "$SO" "$OLD" "test -f ~/$REMOTE_DIR/data/lw.sqlite3"; then
  ssh -S "$SO" "$OLD" "cat ~/$REMOTE_DIR/data/lw.sqlite3" > "$BACKUP" || restart_old
  echo "Копія історії: $BACKUP"
  echo "→ Новий сервер $NEW. Пароль від нього (якщо попросить) вводите тут."
  ssh -o ControlMaster=yes -o ControlPersist=15m -S "$SN" -fN "$NEW" || restart_old
  ssh -S "$SN" "$NEW" "umask 077; cat > ~/$REMOTE_DIR/lw.upload" < "$BACKUP" || restart_old
  ssh -t -S "$SN" "$NEW" "$SUDO_CMD \$S docker stop light-watcher >/dev/null && \$S mv ~/$REMOTE_DIR/lw.upload ~/$REMOTE_DIR/data/lw.sqlite3 && \$S docker start light-watcher >/dev/null" || restart_old
  echo "✅ Історію перенесено."
else
  echo "На старому сервері бази немає — переносити нічого."
fi

sleep 3
if curl -fsS --max-time 8 "http://$NEW_IP:$PORT/healthz" >/dev/null 2>&1; then
  ADDR="$NEW_IP"
  [ "$PORT" = "8000" ] || ADDR="$NEW_IP:$PORT"
  echo
  echo "✅ Новий сервер працює з усією історією. Старий зупинено (не видалено)."
  echo "Тепер у боті: ⚙️ → 🖥 Сервер → 🌍 Змінити адресу → надішліть: $ADDR"
  echo "Події, що сталися за цей час, пристрій тримає в черзі й передасть новому серверу."
  echo "Коли все працює — видаліть старий:  tools/remove.sh $OLD"
else
  echo "⚠️ Новий сервер ззовні не відповідає. Старий зупинено; запустити його знову: ssh $OLD docker start light-watcher"
  exit 2
fi
