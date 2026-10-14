#pragma once

#include <QString>

namespace h2d {
// Install after QApplication has loaded Cocoa and before showing the first tray.
// A failed check leaves the tray hidden rather than exposing an unguarded callback.
bool installCocoaTrayEventGuard(QString *error = nullptr);
}
