#!/bin/bash
# Build the slot B boot image, write it to the phone over SSH, verify it, reboot, wait for SSH to return.
# Usage: deploy.sh [--no-build]
set -euo pipefail
D=/home/casper/Projects/tcl30se-gsi/debian
IMG=${IMG:-$D/out/boot-b-mainline.img}
SSH="ssh -i $HOME/.ssh/tcl30se -o BatchMode=yes -o ConnectTimeout=6 -o StrictHostKeyChecking=accept-new root@172.16.42.1"

laptop_ip() {   # the laptop does not take the phone's DHCP lease on its own
  local i; i=$(ip -br link | awk '/enp.*u[0-9]/{print $1}' | head -1)
  [ -n "$i" ] && { ip -4 addr show dev "$i" | grep -q 172.16.42.2 || sudo -n ip addr add 172.16.42.2/24 dev "$i"; } || true
}

[ "${1:-}" = --no-build ] || $D/mkboot.sh | tail -2
laptop_ip
N=$(stat -c %s $IMG); M=$(md5sum $IMG | cut -c1-32)
# Refuse to write anywhere but boot_b (p47 on this phone's partition table).
cat $IMG | $SSH 'P=/dev/disk/by-partlabel/boot_b; [ "$(readlink -f $P)" = /dev/mmcblk0p47 ] && dd of=$P bs=1M conv=fsync 2>/dev/null'
R=$($SSH "head -c $N /dev/disk/by-partlabel/boot_b | md5sum | cut -c1-32")
[ "$R" = "$M" ] || { echo "boot_b MISMATCH: $R vs $M, not rebooting"; exit 1; }
echo "boot_b verified ($M)"
$SSH 'systemctl reboot' 2>/dev/null || true
sleep 8
for i in $(seq 90); do lsusb | grep -q 0525:a4a2 && break; sleep 1; done
sleep 2
for k in $(seq 25); do
  laptop_ip
  if timeout 8 $SSH 'echo "up: $(uname -r), systemd $(systemctl is-system-running), uptime $(cut -d" " -f1 /proc/uptime)s"' 2>/dev/null; then exit 0; fi
  sleep 3
done
echo "phone did not come back on SSH"; exit 1
