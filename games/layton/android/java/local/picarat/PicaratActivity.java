// Picarat's Android activity: SDL's activity (which loads libSDL3.so and the
// app, libmain.so) with one change to the screen's safe area.
package local.picarat;

import android.content.Context;
import android.graphics.Insets;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;
import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public class PicaratActivity extends SDLActivity {
    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new Surface(context);
    }

    // SDL also counts the system gesture zones (the strips at the screen's
    // sides where a swipe means "back") as unsafe, which would leave empty
    // margins. Taps work there, so the app only keeps clear of the system
    // bars and the camera cut-out.
    static class Surface extends SDLSurface {
        Surface(Context context) {
            super(context);
        }

        @Override
        public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
            WindowInsets result = super.onApplyWindowInsets(v, insets);
            if (Build.VERSION.SDK_INT >= 30 /* Android 11 */) {
                Insets safe = insets.getInsets(WindowInsets.Type.systemBars() |
                                               WindowInsets.Type.displayCutout());
                SDLActivity.onNativeInsetsChanged(safe.left, safe.right, safe.top, safe.bottom);
            }
            return result;
        }
    }
}
