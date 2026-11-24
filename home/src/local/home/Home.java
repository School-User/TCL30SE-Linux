package local.home;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.WallpaperManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Color;
import android.graphics.RenderEffect;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextClock;
import android.widget.TextView;
import android.widget.Toast;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Consumer;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

// Text-only home: clock, date, pinned apps, folders (tap to expand), then every other app; search at the bottom.
// State is two JSON strings in SharedPreferences ("pins", "folders").
// ponytail: app list reloads in onResume (no package-change receiver); pins keyed by package, not component.
public class Home extends Activity {
  static class App { String label, pkg; Intent launch; }
  static class Row { App app; String folder; String text; boolean child, pinned; }

  final List<App> all = new ArrayList<>();
  final Map<String, App> byPkg = new HashMap<>();
  final List<Row> shown = new ArrayList<>();
  final List<String> pins = new ArrayList<>();
  final Set<String> open = new HashSet<>();   // expanded folder names (not persisted)
  Map<String, List<String>> folders = new LinkedHashMap<>();
  SharedPreferences sp;
  // name, size (sp), typeface family
  // Only families that Typeface.create() really resolves on this GSI (checked on the phone). The OEM-only ones in
  // fonts.xml (inter, lato, barlow, zilla-slab, fraunces, roboto-flex, big-shoulders, manrope) silently fall back to Roboto.
  static final String[][] CLOCKS = {
    {"Big", "64", "sans-serif"}, {"Thin", "64", "sans-serif-thin"}, {"Light", "64", "sans-serif-light"},
    {"Bold", "64", "sans-serif-black"}, {"Condensed", "64", "sans-serif-condensed"},
    {"Mono", "48", "monospace"}, {"Typewriter", "48", "courier"}, {"Serif", "56", "serif"},
    {"Rubik", "60", "rubik-medium"}, {"Source Sans", "60", "source-sans-pro"},
    {"Script", "60", "cursive"}, {"Casual", "56", "casual"}, {"Small", "32", "sans-serif"}};
  TextClock clock;
  LinearLayout root;
  ImageView bg;
  static final int[] BLUR_DP = {15, 30, 50}, LOCK_PASSES = {2, 3, 4};   // Low / Medium / High
  static final String[] STRENGTH = {"Low", "Medium", "High"};
  EditText search;
  ListView list;
  BaseAdapter adapter;
  float d;

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    sp = getSharedPreferences("home", MODE_PRIVATE);
    readPrefs();
    d = getResources().getDisplayMetrics().density;
    int pad = (int) (24 * d);

    root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);
    root.setBackgroundColor(0x99101418);   // frosted: translucent tint over the blurred wallpaper
    root.setPadding(pad, pad, pad, pad / 2);

    clock = new TextClock(this);   // follows the system 12/24h setting
    clock.setTextColor(Color.WHITE);
    styleClock();
    clock.setOnLongClickListener(v -> { settingsMenu(); return true; });   // long-press the clock for settings
    TextClock date = new TextClock(this);
    date.setFormat12Hour("EEEE, MMM d");
    date.setFormat24Hour("EEEE, MMM d");
    date.setTextSize(16);
    date.setTextColor(0xFF999999);
    root.addView(clock);
    root.addView(date);

    list = new ListView(this);
    list.setDivider(null);
    list.setVerticalScrollBarEnabled(false);
    adapter = new BaseAdapter() {
      public int getCount() { return shown.size(); }
      public Object getItem(int i) { return shown.get(i); }
      public long getItemId(int i) { return i; }
      public View getView(int i, View v, ViewGroup parent) {
        Row r = shown.get(i);
        TextView t = v instanceof TextView ? (TextView) v : new TextView(Home.this);
        t.setText(r.text);
        t.setTextSize(22);
        t.setTextColor(r.folder != null ? 0xFF8AB4F8 : Color.WHITE);
        t.setTypeface(null, r.pinned ? Typeface.BOLD : Typeface.NORMAL);
        t.setPadding((int) ((r.child ? 28 : 0) * d), (int) (10 * d), 0, (int) (10 * d));
        return t;
      }
    };
    list.setAdapter(adapter);
    list.setOnItemClickListener((p, v, pos, id) -> {
      Row r = shown.get(pos);
      if (r.folder == null) { open(r.app); return; }
      if (!open.remove(r.folder)) open.add(r.folder);
      filter();
    });
    list.setOnItemLongClickListener((p, v, pos, id) -> {
      Row r = shown.get(pos);
      if (r.folder != null) folderMenu(r.folder); else appMenu(r.app);
      return true;
    });
    root.addView(list, new LinearLayout.LayoutParams(-1, 0, 1f));

    search = new EditText(this);
    search.setHint("search");
    search.setHintTextColor(0xFF666666);
    search.setTextColor(Color.WHITE);
    search.setSingleLine();
    search.setImeOptions(EditorInfo.IME_ACTION_GO);
    search.addTextChangedListener(new TextWatcher() {
      public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
      public void onTextChanged(CharSequence s, int a, int b, int c) {}
      public void afterTextChanged(Editable e) { filter(); }
    });
    search.setOnEditorActionListener((v, action, ev) -> {
      for (Row r : shown) if (r.app != null) { open(r.app); break; }
      return true;
    });
    root.addView(search);
    // Frosted: the phone has window blur off, so blur the wallpaper ourselves (RenderEffect, API 31+) under the tinted list.
    // ponytail: wallpaper read once per onCreate; a wallpaper change shows after the next launcher restart.
    FrameLayout frame = new FrameLayout(this);
    bg = new ImageView(this);
    bg.setScaleType(ImageView.ScaleType.CENTER_CROP);
    WallpaperManager wm = WallpaperManager.getInstance(this);
    try { bg.setImageDrawable(wm.getDrawable()); }   // needs READ_EXTERNAL_STORAGE (install with -g)
    catch (SecurityException e) { bg.setImageDrawable(wm.getBuiltInDrawable()); }
    frame.addView(bg, new FrameLayout.LayoutParams(-1, -1));
    frame.addView(root, new FrameLayout.LayoutParams(-1, -1));
    setContentView(frame);
    applyLook();
  }

  @Override protected void onResume() {
    super.onResume();
    load();
  }

  @Override protected void onNewIntent(Intent i) {   // HOME pressed while already home: reset
    super.onNewIntent(i);
    search.setText("");
    open.clear();
    filter();
    list.setSelection(0);
    ((InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE))
        .hideSoftInputFromWindow(search.getWindowToken(), 0);
  }

  // ---- look (settings) ----

  int strength() { return sp.getInt("blur", 1); }

  void applyLook() {   // home: frosted (blurred wallpaper + tint) or plain black
    boolean on = sp.getBoolean("homeblur", true);
    bg.setVisibility(on ? View.VISIBLE : View.GONE);
    bg.setRenderEffect(on ? RenderEffect.createBlurEffect(BLUR_DP[strength()] * d, BLUR_DP[strength()] * d,
        Shader.TileMode.CLAMP) : null);
    root.setBackgroundColor(on ? 0x99101418 : Color.BLACK);
  }

  void applyLock() {   // lock screen: blurred copy of the wallpaper, or back to the normal one
    final boolean on = sp.getBoolean("lockblur", false);
    final int passes = LOCK_PASSES[strength()];
    new Thread(() -> {
      boolean ok = LockWall.apply(this, on, passes);
      runOnUiThread(() -> Toast.makeText(this, ok ? "Lock screen updated" : "Lock screen change failed",
          Toast.LENGTH_SHORT).show());
    }).start();
  }

  void settingsMenu() {
    final boolean home = sp.getBoolean("homeblur", true), lock = sp.getBoolean("lockblur", false);
    List<String> labels = new ArrayList<>();
    List<Runnable> acts = new ArrayList<>();
    labels.add("Home blur: " + (home ? "on" : "off"));
    acts.add(() -> { sp.edit().putBoolean("homeblur", !home).apply(); applyLook(); });
    labels.add("Lock screen blur: " + (lock ? "on" : "off"));
    acts.add(() -> { sp.edit().putBoolean("lockblur", !lock).apply(); applyLock(); });
    labels.add("Blur strength: " + STRENGTH[strength()]);
    acts.add(this::strengthMenu);
    labels.add("Clock style...");
    acts.add(this::clockMenu);
    menu("Settings", labels, acts);
  }

  void strengthMenu() {
    List<String> labels = new ArrayList<>();
    List<Runnable> acts = new ArrayList<>();
    for (int i = 0; i < STRENGTH.length; i++) {
      final int n = i;
      labels.add(STRENGTH[i]);
      acts.add(() -> {
        sp.edit().putInt("blur", n).apply();
        applyLook();
        if (sp.getBoolean("lockblur", false)) applyLock();
      });
    }
    menu("Blur strength", labels, acts);
  }

  void clockMenu() {
    List<String> labels = new ArrayList<>();
    List<Runnable> acts = new ArrayList<>();
    for (int i = 0; i < CLOCKS.length; i++) {
      final int n = i;
      labels.add(CLOCKS[i][0]);
      acts.add(() -> { sp.edit().putInt("clock", n).apply(); styleClock(); });
    }
    menu("Clock style", labels, acts);
  }

  void styleClock() {
    String[] s = CLOCKS[Math.min(sp.getInt("clock", 0), CLOCKS.length - 1)];
    clock.setTextSize(Float.parseFloat(s[1]));
    clock.setTypeface(Typeface.create(s[2], Typeface.NORMAL));
  }

  // ---- data ----

  void load() {
    PackageManager pm = getPackageManager();
    all.clear();
    byPkg.clear();
    for (ResolveInfo r : pm.queryIntentActivities(
        new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER), 0)) {
      String pk = r.activityInfo.packageName;
      // Lawnchair stays installed as the gesture/recents provider; not a place to go.
      if (pk.equals(getPackageName()) || pk.equals("app.lawnchair")) continue;
      App a = new App();
      a.label = r.loadLabel(pm).toString();
      a.pkg = pk;
      a.launch = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
          .setComponent(new ComponentName(pk, r.activityInfo.name))
          .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
      all.add(a);
    }
    Collections.sort(all, (x, y) -> x.label.compareToIgnoreCase(y.label));
    for (App a : all) if (!byPkg.containsKey(a.pkg)) byPkg.put(a.pkg, a);
    if (!sp.contains("pins")) {   // first run: pin the basics
      for (String want : new String[] {"phone", "messaging", "camera", "settings"})
        for (App a : all) if (a.label.equalsIgnoreCase(want)) { pins.add(a.pkg); break; }
      writePrefs();
    }
    filter();
  }

  void readPrefs() {
    try {
      JSONArray pa = new JSONArray(sp.getString("pins", "[]"));
      for (int i = 0; i < pa.length(); i++) pins.add(pa.getString(i));
      JSONArray fa = new JSONArray(sp.getString("folders", "[]"));
      for (int i = 0; i < fa.length(); i++) {
        JSONObject o = fa.getJSONObject(i);
        List<String> l = new ArrayList<>();
        JSONArray aa = o.getJSONArray("apps");
        for (int j = 0; j < aa.length(); j++) l.add(aa.getString(j));
        folders.put(o.getString("name"), l);
      }
    } catch (JSONException e) { /* corrupt prefs: start empty */ }
  }

  void writePrefs() {
    try {
      JSONArray fa = new JSONArray();
      for (Map.Entry<String, List<String>> e : folders.entrySet())
        fa.put(new JSONObject().put("name", e.getKey()).put("apps", new JSONArray(e.getValue())));
      sp.edit().putString("pins", new JSONArray(pins).toString())
          .putString("folders", fa.toString()).apply();
    } catch (JSONException e) { /* cannot happen: plain strings */ }
  }

  void unfile(String pkg) {   // an app is pinned OR in one folder, never both
    pins.remove(pkg);
    for (List<String> l : folders.values()) l.remove(pkg);
  }

  String folderOf(String pkg) {
    for (Map.Entry<String, List<String>> e : folders.entrySet())
      if (e.getValue().contains(pkg)) return e.getKey();
    return null;
  }

  void changed() { writePrefs(); filter(); }

  // ---- list ----

  Row appRow(App a, boolean child, boolean pinned) {
    Row r = new Row();
    r.app = a; r.text = a.label; r.child = child; r.pinned = pinned;
    return r;
  }

  void filter() {
    String q = search.getText().toString().trim().toLowerCase();
    shown.clear();
    if (!q.isEmpty()) {   // search is flat: everything matches, wherever it is filed
      for (App a : all) if (a.label.toLowerCase().contains(q)) shown.add(appRow(a, false, pins.contains(a.pkg)));
    } else {
      Set<String> used = new HashSet<>();
      for (String p : pins) {
        App a = byPkg.get(p);
        if (a != null) { shown.add(appRow(a, false, true)); used.add(p); }
      }
      for (Map.Entry<String, List<String>> e : folders.entrySet()) {
        boolean isOpen = open.contains(e.getKey());
        int n = 0;
        int at = shown.size();
        shown.add(null);   // folder row goes here once the count is known
        for (String p : e.getValue()) {
          App a = byPkg.get(p);
          if (a == null) continue;
          n++; used.add(p);
          if (isOpen) shown.add(appRow(a, true, false));
        }
        Row f = new Row();
        f.folder = e.getKey();
        f.text = (isOpen ? "- " : "+ ") + e.getKey() + " (" + n + ")";
        shown.set(at, f);
      }
      for (App a : all) if (!used.contains(a.pkg)) shown.add(appRow(a, false, false));
    }
    adapter.notifyDataSetChanged();
  }

  // ---- menus ----

  void menu(String title, List<String> labels, List<Runnable> acts) {
    new AlertDialog.Builder(this).setTitle(title)
        .setItems(labels.toArray(new String[0]), (dlg, i) -> acts.get(i).run()).show();
  }

  void ask(String title, String init, Consumer<String> ok) {
    EditText t = new EditText(this);
    t.setText(init);
    t.setSelectAllOnFocus(true);   // typing replaces the old name
    t.setSingleLine();
    new AlertDialog.Builder(this).setTitle(title).setView(t)
        .setPositiveButton("OK", (dlg, w) -> {
          String s = t.getText().toString().trim();
          if (!s.isEmpty()) ok.accept(s);
        }).setNegativeButton("Cancel", null).show();
  }

  void appMenu(final App a) {
    List<String> labels = new ArrayList<>();
    List<Runnable> acts = new ArrayList<>();
    final boolean pinned = pins.contains(a.pkg);
    final String cur = folderOf(a.pkg);
    labels.add(pinned ? "Unpin" : "Pin to top");
    acts.add(() -> { unfile(a.pkg); if (!pinned) pins.add(a.pkg); changed(); });
    for (String f : folders.keySet()) {
      if (f.equals(cur)) {
        labels.add("Remove from " + f);
        acts.add(() -> { unfile(a.pkg); changed(); });
      } else {
        labels.add("Move to " + f);
        acts.add(() -> { unfile(a.pkg); folders.get(f).add(a.pkg); open.add(f); changed(); });
      }
    }
    labels.add("New folder...");
    acts.add(() -> ask("Folder name", "", name -> {
      unfile(a.pkg);
      folders.computeIfAbsent(name, k -> new ArrayList<>()).add(a.pkg);
      open.add(name);
      changed();
    }));
    labels.add("App info");
    acts.add(() -> startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
        Uri.parse("package:" + a.pkg))));
    labels.add("Uninstall");
    acts.add(() -> startActivity(new Intent(Intent.ACTION_DELETE, Uri.parse("package:" + a.pkg))));
    menu(a.label, labels, acts);
  }

  void folderMenu(final String f) {
    List<String> labels = new ArrayList<>();
    List<Runnable> acts = new ArrayList<>();
    labels.add("Rename");
    acts.add(() -> ask("Folder name", f, name -> {
      if (name.equals(f) || folders.containsKey(name)) return;
      Map<String, List<String>> n = new LinkedHashMap<>();   // keep folder order
      for (Map.Entry<String, List<String>> e : folders.entrySet())
        n.put(e.getKey().equals(f) ? name : e.getKey(), e.getValue());
      folders = n;
      if (open.remove(f)) open.add(name);
      changed();
    }));
    labels.add("Delete folder (apps go back to the list)");
    acts.add(() -> { folders.remove(f); open.remove(f); changed(); });
    menu(f, labels, acts);
  }

  void open(App a) {
    try { startActivity(a.launch); } catch (Exception e) { /* app vanished; list refreshes on resume */ }
  }
}
