#!/usr/bin/env bash
# Root dd dump of every eMMC partition except userdata, plus boot0/boot1 (preloader)
# and the primary/secondary GPT. Verifies each file's size and sha256 against the device.
set -euo pipefail
cd "$(dirname "$0")"
OUT=backup
mkdir -p "$OUT"
rsu() { adb exec-out "su 0 sh -c '$1'"; }

# name|device|dd extra args|expected bytes
jobs=()
while read -r name size; do
  case "$name" in userdata|mmcblk0) continue ;; esac
  jobs+=("$name|/dev/block/by-name/$name||$size")
done < notes/partition-sizes.txt
DISK_SECTORS=$(rsu 'cat /sys/block/mmcblk0/size' | tr -d '\r')
jobs+=("gpt-primary|/dev/block/mmcblk0|count=64|32768")
jobs+=("gpt-secondary|/dev/block/mmcblk0|skip=$((DISK_SECTORS-64)) count=64|32768")

for j in "${jobs[@]}"; do
  IFS='|' read -r name dev extra size <<<"$j"
  f="$OUT/$name.img"
  bs=4194304; [ -n "$extra" ] && bs=512
  if [ ! -s "$f" ] || [ "$(stat -c%s "$f")" != "$size" ]; then
    rsu "dd if=$dev bs=$bs $extra 2>/dev/null" > "$f"
  fi
  got=$(stat -c%s "$f")
  dev_sha=$(rsu "dd if=$dev bs=$bs $extra 2>/dev/null | sha256sum" | tr -d '\r' | cut -d' ' -f1)
  host_sha=$(sha256sum "$f" | cut -d' ' -f1)
  if [ "$got" = "$size" ] && [ "$dev_sha" = "$host_sha" ]; then
    echo "OK   $name $got $host_sha"
  else
    echo "FAIL $name got=$got want=$size dev=$dev_sha host=$host_sha"
  fi
done | tee notes/backup-verify.txt
! grep -q '^FAIL' notes/backup-verify.txt
