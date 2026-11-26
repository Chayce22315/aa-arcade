package io.itch.chayce22315.spindle;

import android.content.Intent;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

/**
 * spindle's activity: SDL's activity running SDL_main from libmain.so (built by ../../CMakeLists.txt).
 *
 * Command-line arguments can be passed for testing, e.g. the self-test:
 *   adb shell am start -n io.itch.chayce22315.spindle/.SpindleActivity --es args "--selftest"
 */
public class SpindleActivity extends SDLActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Android may destroy and re-create the activity at any time for changes no app can opt out of
        // (for example its resources being re-indexed right after an install). SDL's default answer is to
        // end the process; spindle cleans up completely when main returns, so let SDL run main again.
        // This has to be set here, after the libraries are loaded, because the re-creation can come
        // before main has started.
        if (!SDLActivity.mBrokenLibraries) {
            SDLActivity.nativeSetenv("SDL_ANDROID_ALLOW_RECREATE_ACTIVITY", "1");
        }
    }

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
