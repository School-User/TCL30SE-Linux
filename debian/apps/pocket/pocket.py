#!/usr/bin/python3
# Pocket: a small WebKitGTK browser for the TCL 30 SE (720x1600, scale 2, CPU-only rendering, Sxmo).
# Bottom bar, max 4 tabs (background tabs unloaded), mobile UA, ad/tracker blocking,
# lite mode (no JS), bookmarks + history in sqlite, downloads to ~/Downloads, tabs restored on start.
import json, os, sqlite3, sys, time
import gi
gi.require_version("Gtk", "4.0")
gi.require_version("WebKit", "6.0")
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, Gio, GLib, Gtk, WebKit

APP_ID = "org.casper.Pocket"
DATA = os.path.join(GLib.get_user_data_dir(), "pocket")
CACHE = os.path.join(GLib.get_user_cache_dir(), "pocket")
HOME = "https://html.duckduckgo.com/html/"
SEARCH = "https://html.duckduckgo.com/html/?q="
MAX_TABS = 4
UA_MOBILE = ("Mozilla/5.0 (Linux; Android 14; Pixel 7) AppleWebKit/605.1.15 "
             "(KHTML, like Gecko) Version/17.0 Mobile Safari/605.1.15")
CSS = b"""
.bar { padding: 4px; background: #202124; }
.bar button { min-width: 40px; min-height: 40px; padding: 0 6px; }
.bar entry { min-height: 40px; }
.tabrow { padding: 8px; }
.tabrow label { font-size: 13px; }
progressbar trough, progressbar progress { min-height: 3px; }
"""


def to_url(text):
    t = text.strip()
    if not t:
        return HOME
    if "://" in t or t.startswith("about:"):
        return t
    if " " not in t and "." in t:
        return "https://" + t
    return SEARCH + GLib.uri_escape_string(t, None, True)


class Store:
    """Bookmarks + history in one small sqlite file."""

    def __init__(self):
        os.makedirs(DATA, exist_ok=True)
        self.db = sqlite3.connect(os.path.join(DATA, "pocket.db"))
        self.db.executescript(
            "CREATE TABLE IF NOT EXISTS history(url TEXT PRIMARY KEY, title TEXT, ts REAL);"
            "CREATE TABLE IF NOT EXISTS bookmarks(url TEXT PRIMARY KEY, title TEXT, ts REAL);")

    def visit(self, url, title):
        if url.startswith("http"):
            self.db.execute("INSERT OR REPLACE INTO history VALUES(?,?,?)", (url, title, time.time()))
            self.db.execute("DELETE FROM history WHERE url NOT IN "
                            "(SELECT url FROM history ORDER BY ts DESC LIMIT 2000)")
            self.db.commit()

    def toggle_bookmark(self, url, title):
        if self.db.execute("SELECT 1 FROM bookmarks WHERE url=?", (url,)).fetchone():
            self.db.execute("DELETE FROM bookmarks WHERE url=?", (url,))
            added = False
        else:
            self.db.execute("INSERT INTO bookmarks VALUES(?,?,?)", (url, title, time.time()))
            added = True
        self.db.commit()
        return added

    def rows(self, table, limit=200):
        return self.db.execute(f"SELECT url, title FROM {table} ORDER BY ts DESC LIMIT ?",
                               (limit,)).fetchall()


class Tab:
    def __init__(self, win, url):
        self.win = win
        self.url = url          # remembered while unloaded
        self.title = url
        self.view = None

    def load(self):
        if self.view is None:
            self.view = self.win.new_view()
            self.view.load_uri(self.url)
        return self.view

    def unload(self):
        if self.view is not None:
            self.url = self.view.get_uri() or self.url
            self.title = self.view.get_title() or self.title
            self.view.try_close()
            self.view = None


class Window(Gtk.ApplicationWindow):
    def __init__(self, app, store, filt):
        super().__init__(application=app, title="Pocket")
        self.store, self.filt, self.lite = store, filt, False
        self.tabs, self.cur = [], None
        self.session = WebKit.NetworkSession.new(os.path.join(DATA, "web"), os.path.join(CACHE, "web"))
        self.session.connect("download-started", self.on_download)
        self.ucm = WebKit.UserContentManager()
        if filt:
            self.ucm.add_filter(filt)

        self.stack = Gtk.Stack(vexpand=True)
        self.progress = Gtk.ProgressBar(visible=False)
        self.entry = Gtk.Entry(hexpand=True, input_purpose=Gtk.InputPurpose.URL,
                               placeholder_text="Search or address")
        self.entry.connect("activate", lambda e: self.go(e.get_text()))
        back = Gtk.Button(icon_name="go-previous-symbolic")
        back.connect("clicked", lambda b: self.view() and self.view().go_back())
        self.tabbtn = Gtk.Button(label="1")
        self.tabbtn.connect("clicked", lambda b: self.show_tabs())
        menu = Gtk.MenuButton(icon_name="open-menu-symbolic", menu_model=self.build_menu())
        menu.set_direction(Gtk.ArrowType.UP)
        bar = Gtk.Box(spacing=4, css_classes=["bar"])
        for w in (back, self.entry, self.tabbtn, menu):
            bar.append(w)
        self.bar = bar
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        box.append(self.progress)
        box.append(self.stack)
        box.append(bar)
        self.set_child(box)
        self.tabpage = Gtk.ScrolledWindow()
        self.stack.add_named(self.tabpage, "tabs")
        self.add_actions()

    # ---- views / tabs -------------------------------------------------------
    def new_view(self):
        v = WebKit.WebView(network_session=self.session, user_content_manager=self.ucm)
        s = v.get_settings()
        s.set_user_agent(UA_MOBILE)
        s.set_hardware_acceleration_policy(WebKit.HardwareAccelerationPolicy.NEVER)
        s.set_enable_webgl(False)
        s.set_enable_smooth_scrolling(False)
        s.set_enable_javascript(not self.lite)
        s.set_media_playback_requires_user_gesture(True)
        s.set_enable_back_forward_navigation_gestures(True)
        v.connect("notify::uri", self.on_uri)
        v.connect("notify::title", self.on_title)
        v.connect("notify::estimated-load-progress", self.on_progress)
        v.connect("create", self.on_create)            # target=_blank / "open in new window"
        v.connect("decide-policy", self.on_policy)
        self.stack.add_child(v)
        return v

    def view(self):
        return self.cur.view if self.cur else None

    def open_tab(self, url, switch=True):
        if len(self.tabs) >= MAX_TABS:   # drop the least recently used
            old = self.tabs.pop(0)
            old.unload()
        t = Tab(self, url)
        self.tabs.append(t)
        if switch:
            self.switch(t)
        self.tabbtn.set_label(str(len(self.tabs)))
        return t

    def switch(self, t):
        for o in self.tabs:      # ponytail: only the visible tab keeps a live WebView (RAM)
            if o is not t:
                o.unload()
        self.cur = t
        self.tabs.remove(t)
        self.tabs.append(t)
        v = t.load()
        self.stack.set_visible_child(v)
        self.entry.set_text(t.url if t.url != HOME else "")
        self.tabbtn.set_label(str(len(self.tabs)))

    def close_tab(self, t):
        t.unload()
        self.tabs.remove(t)
        if not self.tabs:
            self.open_tab(HOME)
        elif t is self.cur:
            self.switch(self.tabs[-1])
        self.tabbtn.set_label(str(len(self.tabs)))
        self.show_tabs()

    def show_tabs(self):
        lst = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6, css_classes=["tabrow"])
        for t in reversed(self.tabs):
            if t.view:
                t.url, t.title = t.view.get_uri() or t.url, t.view.get_title() or t.title
            row = Gtk.Box(spacing=6)
            b = Gtk.Button(hexpand=True)
            b.set_child(Gtk.Label(label=(t.title or t.url)[:60], xalign=0, ellipsize=3))
            b.connect("clicked", lambda _b, t=t: self.switch(t))
            x = Gtk.Button(icon_name="window-close-symbolic")
            x.connect("clicked", lambda _b, t=t: self.close_tab(t))
            row.append(b)
            row.append(x)
            lst.append(row)
        new = Gtk.Button(label="+ New tab")
        new.connect("clicked", lambda b: self.open_tab(HOME))
        lst.append(new)
        self.tabpage.set_child(lst)
        self.stack.set_visible_child(self.tabpage)

    def show_list(self, table):
        lst = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4, css_classes=["tabrow"])
        for url, title in self.store.rows(table):
            b = Gtk.Button()
            b.set_child(Gtk.Label(label=(title or url)[:70], xalign=0, ellipsize=3))
            b.connect("clicked", lambda _b, u=url: self.go(u))
            lst.append(b)
        if not lst.get_first_child():
            lst.append(Gtk.Label(label="Nothing here yet"))
        self.tabpage.set_child(lst)
        self.stack.set_visible_child(self.tabpage)

    def go(self, text):
        url = to_url(text)
        if not self.cur:
            self.open_tab(url)
            return
        self.cur.url = url
        self.stack.set_visible_child(self.cur.load())
        self.cur.view.load_uri(url)
        self.cur.view.grab_focus()

    # ---- signals -----------------------------------------------------------
    def on_uri(self, v, _p):
        if v is self.view() and not self.entry.has_focus():
            self.entry.set_text(v.get_uri() or "")

    def on_title(self, v, _p):
        if v.get_uri():
            self.store.visit(v.get_uri(), v.get_title())
        if v is self.view():
            self.set_title(v.get_title() or "Pocket")

    def on_progress(self, v, _p):
        if v is self.view():
            f = v.get_estimated_load_progress()
            self.progress.set_visible(f < 1.0)
            self.progress.set_fraction(f)

    def on_create(self, v, action):
        t = self.open_tab(action.get_request().get_uri())
        return t.view

    def on_policy(self, v, decision, kind):
        if kind == WebKit.PolicyDecisionType.RESPONSE and not decision.is_mime_type_supported():
            decision.download()
            return True
        return False

    def on_download(self, session, dl):
        dl.connect("decide-destination", self.on_dest)
        dl.connect("finished", lambda d: self.toast("Saved " + os.path.basename(d.get_destination() or "")))

    def on_dest(self, dl, name):
        d = os.path.expanduser("~/Downloads")
        os.makedirs(d, exist_ok=True)
        dl.set_destination(os.path.join(d, name or "download"))
        return True

    def toast(self, msg):
        self.set_title(msg)

    # ---- menu --------------------------------------------------------------
    def build_menu(self):
        m = Gio.Menu()
        for label, act in (("Forward", "fwd"), ("Reload", "reload"), ("Bookmark this page", "bm"),
                           ("Bookmarks", "bms"), ("History", "hist"), ("Desktop site", "desk"),
                           ("Lite mode (no JavaScript)", "lite"), ("Zoom +", "zin"), ("Zoom -", "zout"),
                           ("Close tab", "close")):
            m.append(label, "win." + act)
        return m

    def add_actions(self):
        def act(name, fn):
            a = Gio.SimpleAction.new(name, None)
            a.connect("activate", lambda *_: fn())
            self.add_action(a)

        act("fwd", lambda: self.view() and self.view().go_forward())
        act("reload", lambda: self.view() and self.view().reload())
        act("bm", self.bookmark)
        act("bms", lambda: self.show_list("bookmarks"))
        act("hist", lambda: self.show_list("history"))
        act("desk", self.desktop)
        act("lite", self.toggle_lite)
        act("zin", lambda: self.view() and self.view().set_zoom_level(self.view().get_zoom_level() + 0.15))
        act("zout", lambda: self.view() and self.view().set_zoom_level(max(0.3, self.view().get_zoom_level() - 0.15)))
        act("close", lambda: self.cur and self.close_tab(self.cur))

    def bookmark(self):
        v = self.view()
        if v and v.get_uri():
            added = self.store.toggle_bookmark(v.get_uri(), v.get_title())
            self.toast("Bookmarked" if added else "Bookmark removed")

    def desktop(self):
        v = self.view()
        if v:
            s = v.get_settings()
            s.set_user_agent(None if s.get_user_agent() == UA_MOBILE else UA_MOBILE)
            v.reload()

    def toggle_lite(self):
        self.lite = not self.lite
        v = self.view()
        if v:
            v.get_settings().set_enable_javascript(not self.lite)
            v.reload()
        self.toast("Lite mode on" if self.lite else "Lite mode off")

    # ---- session -----------------------------------------------------------
    def save_session(self):
        urls = [(t.view.get_uri() if t.view else t.url) or HOME for t in self.tabs]
        with open(os.path.join(DATA, "session.json"), "w") as f:
            json.dump(urls, f)


class App(Gtk.Application):
    def __init__(self):
        super().__init__(application_id=APP_ID, flags=Gio.ApplicationFlags.HANDLES_OPEN)
        self.win = None

    def do_startup(self):
        Gtk.Application.do_startup(self)
        css = Gtk.CssProvider()
        css.load_from_data(CSS, len(CSS))
        Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), css,
                                                  Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

    def ensure_window(self, done):
        if self.win:
            done()
            return
        self.hold()     # async blocklist load: keep the app alive until the window exists
        store = WebKit.UserContentFilterStore.new(os.path.join(CACHE, "filters"))
        rules = os.path.join(DATA, "blocklist.json")

        def finish(filt):
            self.win = Window(self, Store(), filt)
            self.win.connect("close-request", lambda w: (w.save_session(), False)[1])
            self.win.present()
            self.release()
            done()

        if not os.path.exists(rules):
            finish(None)
            return

        def loaded(st, res):
            try:
                finish(st.load_finish(res))
            except GLib.Error:      # not compiled yet (or list changed): compile it
                data = GLib.Bytes.new(open(rules, "rb").read())
                st.save("ads", data, None, lambda s, r: finish(self._try(s.save_finish, r)))

        store.load("ads", None, loaded)

    @staticmethod
    def _try(fn, res):
        try:
            return fn(res)
        except GLib.Error as e:
            print("blocklist compile failed:", e.message, file=sys.stderr)
            return None

    def do_activate(self):
        def start():
            w = self.win
            if not w.tabs:
                try:
                    urls = json.load(open(os.path.join(DATA, "session.json")))
                except (OSError, ValueError):
                    urls = []
                for u in urls[-MAX_TABS:]:
                    w.open_tab(u, switch=False)
                if w.tabs:
                    w.switch(w.tabs[-1])
                else:
                    w.open_tab(HOME)
            w.present()
        self.ensure_window(start)

    def do_open(self, files, n, hint):
        self.ensure_window(lambda: [self.win.open_tab(f.get_uri()) for f in files] and self.win.present())


if __name__ == "__main__":
    sys.exit(App().run(sys.argv))
