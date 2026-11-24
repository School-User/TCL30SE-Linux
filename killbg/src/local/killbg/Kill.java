package local.killbg;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;

// One tap: root `am kill-all` (cached/background processes only), toast how much RAM came back.
// Full su path on purpose: plain `su` on this phone is the MTK vendor binary.
// Shared by the launcher icon (this activity) and the Quick Settings tile (KillTile).
public class Kill extends Activity {
  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    run(getApplicationContext());
    finish();
  }

  static void run(Context c) {
    final Handler h = new Handler(Looper.getMainLooper());
    new Thread(() -> {
      String msg;
      try {
        long before = avail(c);
        int rc = new ProcessBuilder("/debug_ramdisk/su", "-c", "am kill-all")
            .redirectErrorStream(true).start().waitFor();
        Thread.sleep(800);
        msg = rc == 0 ? "Freed " + (avail(c) - before) / 1048576 + " MB" : "Failed (root denied?) rc=" + rc;
      } catch (Exception e) {
        msg = "Failed: " + e;
      }
      final String m = msg;
      h.post(() -> Toast.makeText(c, m, Toast.LENGTH_SHORT).show());
    }).start();
  }

  static long avail(Context c) {
    ActivityManager.MemoryInfo i = new ActivityManager.MemoryInfo();
    ((ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE)).getMemoryInfo(i);
    return i.availMem;
  }
}
