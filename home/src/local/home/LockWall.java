package local.home;

import android.app.Activity;
import android.app.WallpaperManager;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.os.Bundle;

// Blurs + dims the current wallpaper and sets it as the LOCK wallpaper only (or clears it).
// The launcher's settings menu calls apply(); the activity is the adb entry point:
//   adb shell am start -n local.home/.LockWall [--ez clear true]
// ponytail: blur = repeated down/up scaling (cheap, slightly blocky); real gaussian if it ever looks off.
public class LockWall extends Activity {
  // passes: each pass shrinks 4x before scaling back up, so more passes = blurrier.
  static boolean apply(Context c, boolean on, int passes) {
    WallpaperManager wm = WallpaperManager.getInstance(c);
    try {
      if (!on) { wm.clear(WallpaperManager.FLAG_LOCK); return true; }
      Drawable dr = wm.getDrawable();
      int w = dr.getIntrinsicWidth() / 2, h = dr.getIntrinsicHeight() / 2;
      Bitmap src = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
      dr.setBounds(0, 0, w, h);
      dr.draw(new Canvas(src));
      Bitmap x = src;
      for (int i = 0; i < passes; i++)
        x = Bitmap.createScaledBitmap(x, Math.max(8, x.getWidth() / 4), Math.max(8, x.getHeight() / 4), true);
      Bitmap out = Bitmap.createScaledBitmap(x, w, h, true);
      new Canvas(out).drawColor(0x66101418);   // same tint family as the launcher
      wm.setBitmap(out, null, true, WallpaperManager.FLAG_LOCK);
      return true;
    } catch (Exception e) {
      android.util.Log.e("LockWall", "failed", e);   // lock screen stays as it was
      return false;
    }
  }

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    boolean on = !getIntent().getBooleanExtra("clear", false);
    if (apply(this, on, 3))   // keep the launcher's Settings menu in step with what was applied
      getSharedPreferences("home", MODE_PRIVATE).edit().putBoolean("lockblur", on).putInt("blur", 1).apply();
    finish();
  }
}
