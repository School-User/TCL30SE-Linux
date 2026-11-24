# Cyberdeck session (experimental)

labwc + waybar (top bar: launcher, window list, clock, cpu, mem, wifi, volume, battery estimate) + fuzzel launcher + foot terminal
+ mako notifications, dark green/grey with JetBrains Mono. Same compositor as the XFCE session, so the GPU path (Firefox, hwplay),
USB dock hot-swap, volume buttons and the 180-degree rotation all carry over. Windows open maximized.

Keys: Super+Space or Super+D launcher, Super+Enter terminal, Super+F Firefox (GPU), Super+E files, Super+Q close,
Super+M / Super+Up maximize, Super+Left/Right snap, Alt+Tab switch. Click "deck" on the bar for the launcher.

Install on the phone: `deck-session`, `deck-term` -> /usr/local/bin; `labwc/`, `waybar/`, `fuzzel.ini`, `foot.ini`, `mako.conf` ->
/usr/local/share/cyberdeck/; packages waybar fuzzel foot mako-notifier swaybg wlr-randr fonts-jetbrains-mono.
Switch: `casper-desktop deck` (back: `casper-desktop xfce` or `pocketshell`). Battery: ../desktop-xfce/casper-battery*.

Lock: `deck-lock` (-> /usr/local/bin) = gtklock with a big 12-hour clock and date (`gtklock.css` -> /usr/local/share/cyberdeck/; falls back to swaylock); Super+L, the "lock" button on the bar, and swayidle after 5 min idle (DECK_LOCK_AFTER). Unlocks with the login password on a keyboard; refuses to lock while the account has no password. Emergency unlock over SSH: `pkill -USR1 swaylock`.
