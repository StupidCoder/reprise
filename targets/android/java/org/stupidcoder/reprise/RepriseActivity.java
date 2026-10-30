// Android lifecycle, input and surface ownership are provided by SDL.
package org.stupidcoder.reprise;
public final class RepriseActivity extends org.libsdl.app.SDLActivity {
    // Called through the SDL host boundary, never from a game. No native controls.
    private int gameOrientation = 0;
    public void setGameOrientation(final int policy) {
        runOnUiThread(new Runnable() {
            @Override public void run() { gameOrientation = policy; applyGameOrientation(); }
        });
    }
    // SDL may request orientation again after a window resize. Keep the module's policy.
    @Override public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        runOnUiThread(new Runnable() {
            @Override public void run() { applyGameOrientation(); }
        });
    }
    private void applyGameOrientation() {
        setRequestedOrientation(gameOrientation == 1
            ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
            : gameOrientation == 2 ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
            : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR);
    }
    // System Back gestures may arrive here without a physical key event.
    // Keep navigation inside Reprise instead of finishing the Activity.
    @Override public void onBackPressed() {
        if (mSDLThread != null) {
            onNativeKeyDown(android.view.KeyEvent.KEYCODE_BACK);
            onNativeKeyUp(android.view.KeyEvent.KEYCODE_BACK);
        }
    }
}
