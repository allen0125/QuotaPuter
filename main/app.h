// app.h — top-level UI state machine entry point.
#pragma once

// Run the QuotaPuter UI (overview / detail / settings / Wi-Fi / manage / device).
// Owns the foreground loop and never returns. Call after all subsystems are up.
void app_run(const char *version);
