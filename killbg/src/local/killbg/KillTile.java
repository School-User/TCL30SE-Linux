package local.killbg;

import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

// Quick Settings tile: same action as the app icon. Stateless button, so it always shows "inactive".
public class KillTile extends TileService {
  @Override public void onStartListening() {
    Tile t = getQsTile();
    t.setState(Tile.STATE_INACTIVE);
    t.updateTile();
  }

  @Override public void onClick() {
    Kill.run(getApplicationContext());
  }
}
