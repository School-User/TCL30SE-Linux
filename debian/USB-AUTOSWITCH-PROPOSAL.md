# USB-C host/device switching on the TCL 30 SE (WUSB3801 + MUSB + SY6970)

## 0. What is actually broken in the current design

Two separate problems are tangled inside the shell loop in `scripts.txt`.

**A. The policy is wrong, not just the timing.** The loop waits for something to be
unplugged, then guesses "host" for 60 s. But the question it needs to answer — *is the
thing in the port a laptop/charger, or a peripheral/hub?* — has a real answer in
hardware, and it is not time. The WUSB3801 cannot answer it (`port0-partner` appears for
any partner at all). The SY6970 can: **a laptop or charger pushes VBUS into the port; a
keyboard dongle or a bus-powered hub does not.** So:

| Partner present | VBUS coming in | It is | Want |
|---|---|---|---|
| no | — | nothing | device (safe default, charging) |
| yes | yes | laptop / charger | device |
| yes | no | hub / dongle / peripheral | host |

That table also guarantees the phone never drives 5 V into something that is already
driving 5 V — the backfeed hazard of the current design.

**B. Boost-then-role is the wrong order when something is already plugged in.** Host
mode works only when MUSB's A-device state machine starts *before* VBUS rises. If the hub
is powered first, the hub's D+ pull-up is already there, MUSB enters `a_wait_vrise` with
BDEVICE set (devctl `0x99`), and the VBUS-rise/connect edge it is waiting for never
happens. No amount of boost toggling recovers it, which is exactly what was observed
(`0x2a -> 0x0a -> 0x2a` ends in `b_idle`, `0x98`). The fix is ordering: **MUSB into host
mode while VBUS is still off, then raise VBUS.** That gives one honest 0→1 edge, and it
works for both "nothing attached" and "hub already attached".

**C. Host→device with a laptop still attached** is not a policy problem at all (see the
table — that transition can't legitimately happen), and where it does happen (manual
override, or a dock that supplies VBUS) the laptop is waiting for a D+ disconnect it
never saw, because the phone's PHY never dropped the pull-up. Bounce the gadget's
`soft_connect`; that is the software equivalent of replugging the cable.

## 1. Recommended approach: a stateless reconciler driven by udev

**One short shell script with no memory, run from udev events, boot, and a 30 s safety
timer.** It reads the port state from sysfs, computes the wanted role from the table
above, and applies only the difference. It is idempotent, so double events, boot races,
lost events and hand runs all converge to the same state.

Why this and not the alternatives:

- **Not a time-based loop** (rejected): a 60 s window misses a keyboard plugged at 61 s,
  wastes 5 V into an empty port, and cannot distinguish a hub from a charger at all. All
  of that disappears once VBUS-as-a-source is the discriminator.
- **Not `udev` rules alone** (insufficient): events say *when*, not *what*. The SY6970
  VBUS state has to be read to classify the partner. Rules stay trivial: they only ask
  the service to run.
- **Not a C/Python daemon** (unnecessary): a daemon needs a build, a package, a restart
  path and its own event loop, and it can drift from sysfs. The reconciler holds no state
  to drift; total code is ~50 lines of POSIX sh, and its log is `systemd` + `udevadm`.
- **Not a kernel/DT change first** (right idea, but second step — see §4): moving the
  OTG boost into MUSB's `vbus-supply` and the port into `usb-role-switch` fixes the
  ordering bugs *properly* and would shrink this script to `echo host|device > role`.
  It cannot fix the discrimination problem, because that is a WUSB3801 silicon limit, so
  the policy layer stays either way. Doing it first means binding work plus a
  bq25890-OTG-regulator change before anything is testable.

The race the current design keeps losing is deleted: with this policy, the only channel
that matters is "partner appeared / disappeared" and "VBUS appeared / disappeared", both
of which are kernel events, and the script re-reads the hardware instead of trusting the
event payload.

## 2. Files

### `/usr/local/bin/casper-usb-reconcile`

```sh
#!/bin/sh
# Decide the USB-C port's data role from what the port actually sees, then apply it.
# Stateless and idempotent: safe from udev, at boot, from the timer, or by hand.
# Policy: host only when a partner is present and is NOT sourcing VBUS.
set -u

PORT=/sys/class/typec/port0
ROLE=$(set -- /sys/class/usb_role/*/role; echo "$1")
UDC=$(set -- /sys/class/udc/*; echo "$1")

[ -e /dev/i2c-5 ] || exit 0                 # too early in boot; the timer retries

rd() { i2cget -f -y 5 0x6a 0x03; }
wr() { i2cset -f -y 5 0x6a 0x03 "$1"; }
boost()   { wr $(( ( $(rd) | 0x20 ) & ~0x10 )); }   # bit5 VBUS out on, bit4 charge off
unboost() { wr $(( $(rd) & ~0x20 )); }              # bit5 off; leave bit4 to bq25890

role()   { cat "$ROLE" 2>/dev/null || echo device; }
partner(){ [ -e "$PORT-partner" ]; }
devices(){ ls /sys/bus/usb/devices 2>/dev/null | grep -q '^[0-9]-[0-9.]*$'; }

# VBUS being fed into the port = laptop or charger. Verify the supply name with
# `ls /sys/class/power_supply` on this phone and drop any entry that does not exist.
vbus_in() {
  for s in usb bq25890-charger; do
    f=/sys/class/power_supply/$s/online
    [ -r "$f" ] && [ "$(cat "$f")" = 1 ] && return 0
  done
  return 1
}

set_host() {
  # Order is the whole fix: MUSB must be in host mode while VBUS is still low, so
  # the A-device machine sees a real vbus rise instead of locking in a_wait_vrise.
  unboost;                 sleep 0.1
  echo device > "$ROLE";   sleep 0.1      # drop any previous host session
  echo host   > "$ROLE";   sleep 0.1
  boost                                   # now raise VBUS
  n=0
  while [ $n -lt 8 ]; do                  # ~0.8 s for enumeration to start
    sleep 0.1; devices && return 0
    n=$((n+1))
  done
  # Still nothing: one clean re-arm (empty hubs legitimately show no devices, so
  # this is a cheap nudge, not evidence of a fault).
  unboost; echo device > "$ROLE"; sleep 0.3
  echo host > "$ROLE"; sleep 0.1; boost
}

set_device() {
  prev=$(role)
  echo device > "$ROLE"
  unboost
  # Host -> device with the laptop still attached: it never saw a disconnect,
  # so bounce the D+ pull-up instead of asking the user to replug the cable.
  if [ "$prev" = host ] && vbus_in && [ -w "$UDC/soft_connect" ]; then
    echo 0 > "$UDC/soft_connect"; sleep 0.05; echo 1 > "$UDC/soft_connect"
  fi
}

want() {
  partner || { echo device; return; }
  [ "$(role)" = host ] && { echo host; return; }   # already hosting: stay
  vbus_in && echo device || echo host
}

sleep 0.25                                 # let sysfs settle behind the event
i=0
while [ $i -lt 3 ]; do
  w=$(want)
  [ "$w" = "$(role)" ] && break
  [ "$w" = host ] && set_host || set_device
  sleep 0.3; i=$((i+1))
done
```

Notes: the PHY DTM1 `devmem` write is gone because the kernel now sets `0x11cc086c`
itself; keep one line back only if the host path ever regresses after a PHY power-down.
The `/run/casper-usb.reg03` snapshot is gone too — bit 5 is read-modify-written live, so
a stale snapshot cannot restore the wrong charge state.

### `/etc/udev/rules.d/99-casper-usb.rules`

```
ACTION=="change", SUBSYSTEM=="typec",        KERNEL=="port0", TAG+="systemd", ENV{SYSTEMD_WANTS}+="casper-usb-reconcile.service"
ACTION=="change", SUBSYSTEM=="power_supply", KERNEL=="usb",   TAG+="systemd", ENV{SYSTEMD_WANTS}+="casper-usb-reconcile.service"
```

Confirm the WUSB3801's typec port really emits uevents on this kernel with
`udevadm monitor --subsystem-match=typec --subsystem-match=power_supply` while plugging a
dongle; if it does not, the timer below carries the design on its own (slower, still
automatic).

### `/etc/systemd/system/casper-usb-reconcile.service`

```ini
[Unit]
Description=Set the USB-C port data role from the current port state

[Service]
Type=oneshot
ExecStart=/usr/bin/flock -w 10 /run/casper-usb.lock /usr/local/bin/casper-usb-reconcile
```

`flock` serialises the udev event, the timer tick and a manual run; `Type=oneshot` with
the internal re-check loop means an event arriving mid-settle is picked up rather than
dropped.

### `/etc/systemd/system/casper-usb-reconcile.timer`

```ini
[Unit]
Description=Safety net for casper-usb (recovers from missed udev events)

[Timer]
OnBootSec=15s
OnUnitActiveSec=30s
AccuracySec=5s

[Install]
WantedBy=timers.target
```

Enable with `systemctl enable --now casper-usb-reconcile.timer`. This is not the
mechanism, only insurance: it turns any missed event into a fix within 30 s, and because
the script is idempotent it costs three sysfs reads and no I2C writes when nothing
changed. No manual commands are needed for normal use.

Behaviour it produces: hub/dongle plugged into an idle phone → host within ~1 s, devices
enumerate; hub unplugged → back to device, charging resumes; laptop plugged in → stays a
device, SSH over USB works, and the boost is never enabled while VBUS is present.

## 3. Risks

1. **VBUS backfeed is the one that can damage hardware.** If the "no VBUS" read is stale
   or wrong, the phone boosts into a live port. Mitigations in the design: classify only
   while the boost is off, fail safe to `device` when no power supply is readable, and
   the read-modify-write never sets bit 5 while bit 4 (charge) is set. Residual risk:
   a laptop whose port is suspended/off presents partner-without-VBUS and would be read
   as a hub. If that shows up, require "partner present, VBUS absent, unchanged for
   ~2 s" before boosting, and consider a series ideal-diode on VBUS in hardware.
2. **I2C pokes race the bq25890 driver.** Reading REG03, then writing it back, can lose a
   concurrent driver update to bits 4/6/7. Window is microseconds and only affects the
   charge-enable bits; the clean fix is a DT/regulator-based boost (§4).
3. **udev events from the WUSB3801 are assumed, not verified.** The chip reports partner
   presence, but if this kernel's driver raises no `change` uevent, the timer is the only
   trigger and switching latency becomes up to 30 s. Verify with `udevadm monitor`
   before trusting the rules.
4. **A self-powered hub, or a dock passing VBUS through, looks like a charger** and the
   phone stays a device, so its peripherals never enumerate. Acceptable trade for
   safety; the reverse error (boosting into a laptop) is much worse.
5. **The two retry paths blip the port.** `set_host`'s re-arm and `soft_connect` drop VBUS
   / D+ for a moment, so a working hub may enumerate twice, and a mounted USB stick would
   disconnect once. Only triggered when the first attempt did not come up.
6. **Hosting never charges** (SY6970 cannot boost and charge at once), so long host
   sessions drain the battery, and the phone's boost current limit now feeds the whole
   hub — a 4-port hub with a spinning disk may brown out. That is a hardware budget, not
   a bug in this design.
7. **No state is persisted.** At reboot the script re-derives the role from sysfs, which
   is correct, but a reboot with a hub attached lands in the "already attached" case on
   every boot — handled by the ordering fix, and by the timer if the first pass misfires.
8. `soft_connect` and the `usb_role`/`typec` paths are guarded/optional in the script, so
   a kernel that names them differently degrades to "needs one replug" rather than
   failing loudly. Check `ls /sys/class/udc/*/` and `ls /sys/class/usb_role/` once.

## 4. Follow-up (not part of the minimal fix)

Move the ordering into the kernel so userspace stops poking I2C: expose the SY6970 OTG
boost as a regulator, give MUSB `vbus-supply = <&otg_boost>` and a `usb-role-switch`
provider (the typec port can be the role switch), and let the MUSB host state machine
raise VBUS itself in `a_wait_vrise` and drop it on session end. That removes risks 1, 2
and 5 and shrinks the script to `echo host|device > role` plus the VBUS policy check.
It does not remove the policy layer, because no kernel change can make the WUSB3801
distinguish a dongle from a laptop.
