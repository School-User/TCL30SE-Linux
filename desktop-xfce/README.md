# Cyberdeck desktop (experimental)

labwc (Wayland, software-composited like the PocketShell session) + XFCE panel, desktop, Thunar, notifications and settings.
Chosen because it keeps the Wayland GPU path (Firefox WebRender, hwplay) that an X11 XFCE session would lose.

Install on the phone: `xfce-session` -> /usr/local/bin, `labwc/` -> /usr/local/share/xfce-deck/labwc, then `casper-desktop xfce`.
Packages: labwc xfce4-panel xfdesktop4 thunar xfce4-settings xfce4-terminal xfce4-whiskermenu-plugin xfce4-pulseaudio-plugin xfce4-notifyd wlr-randr gvfs.
Wallpaper: swaybg, `~/Pictures/colorado.jpg` (xfdesktop does not paint backdrops on Wayland). Switching away from pocketshell leaves its restart loop running, so `casper-desktop xfce` kills it (it also creates the "gpu-disabled" flag when it crashes without sway; `casper-desktop pocketshell` clears it).
Scale: `XFCE_DECK_SCALE` (default 1.5). Switch back with `casper-desktop pocketshell`.

fastfetch: `casper-gpu-name` (-> /usr/local/bin) shows the PowerVR GPU; in ~/.config/fastfetch/config.jsonc replace `"gpu",` with `{ "type": "command", "key": "GPU", "text": "casper-gpu-name" },` (fastfetch only probes Mesa and would say llvmpipe).

Battery: `casper-battery` (voltage estimate, ~NN%, + = charging), `casper-battery-label` (layer-shell label on the panel; the genmon plugin cannot embed on Wayland), `casper-battery-watch` (notify at 15% and 5%). All -> /usr/local/bin; needs gir1.2-gtklayershell-0.1. Default browser: update-alternatives x-www-browser/www-browser -> /usr/local/bin/firefox-gpu (the dock browser button failed with "www-browser not found").
