package io.itch.chayce22315.spindle;

import android.content.Intent;

import org.libsdl.app.SDLActivity;

/**
 * spindle's activity: SDL's activity running SDL_main from libmain.so (built by ../../CMakeLists.txt).
 *
 * Command-line arguments can be passed for testing, e.g. the self-test:
 *   adb shell am start -n io.itch.chayce22315.spindle/.SpindleActivity --es args "--selftest"
 */
public class SpindleActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        String args = intent != null ? intent.getStringExtra("args") : null;
        if (args == null || args.trim().isEmpty()) {
            return new String[0];
        }
        return args.trim().split("\\s+");
    }
}
