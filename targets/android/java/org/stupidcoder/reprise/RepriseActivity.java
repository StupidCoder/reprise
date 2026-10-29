// Android lifecycle, input and surface ownership are provided by SDL.
package org.stupidcoder.reprise;
public final class RepriseActivity extends org.libsdl.app.SDLActivity {
    // System Back gestures may arrive here without a physical key event.
    // Keep navigation inside Reprise instead of finishing the Activity.
    @Override public void onBackPressed() {
        if (mSDLThread != null) {
            onNativeKeyDown(android.view.KeyEvent.KEYCODE_BACK);
            onNativeKeyUp(android.view.KeyEvent.KEYCODE_BACK);
        }
    }
}
