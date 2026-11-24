# Pocket — browser for the TCL 30 SE (Debian + Sxmo)

WebKitGTK 6 (2.52) + GTK 4 in one Python file. CPU-only rendering (simpledrm): launcher sets
GSK_RENDERER=cairo, WEBKIT_DISABLE_DMABUF_RENDERER=1, WEBKIT_DISABLE_COMPOSITING_MODE=1, GTK_A11Y=none.

Installed on the phone:
- /usr/local/lib/pocket/pocket.py, /usr/local/bin/pocket (launcher), /usr/local/bin/pocket-blocklist
- /usr/share/applications/org.casper.Pocket.desktop; default http/https/html handler in ~siddh/.config/mimeapps.list
- Sxmo app menu: ~siddh/.config/sxmo/hooks/sxmo_hook_apps.sh (copy of default + "Pocket" line)
- Data: ~/.local/share/pocket (pocket.db bookmarks/history, session.json tabs, blocklist.json), downloads ~/Downloads

Features: bottom bar (Back, address/search, tab count, menu), max 4 tabs with only the visible one loaded,
mobile UA (menu: Desktop site), Peter Lowe blocklist (3562 third-party rules; refresh with
`pocket-blocklist`, then delete ~/.cache/pocket/filters to recompile), Lite mode (JS off), zoom,
bookmarks/history lists, target=_blank opens a tab, downloads, tab restore.

Tested 2026-11-11: en.m.wikipedia.org renders clean; cnn.com loads (<40 s, WebKitWebProcess ~500 MB, load ~1.3).
Not done: volume-key scrolling (Sxmo owns those keys), hide bar on scroll, per-site lite mode.
