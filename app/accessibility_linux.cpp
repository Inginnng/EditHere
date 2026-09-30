#include <atspi/atspi.h>
#include "platform.h"
#include <QElapsedTimer>
#include <QSet>
#include <algorithm>
namespace h2d {
QVector<Candidate> linuxAccessibleElements(QPoint point, qint64 excluded) {
    QVector<Candidate> result;
    // This function runs in the existing --inspect child, which the overlay
    // bounds with a deadline. AT-SPI timeouts also limit unresponsive clients.
    if (atspi_init() != 0) return result;
    atspi_set_timeout(150, 200);
    QElapsedTimer timer; timer.start();
    auto *desktop = atspi_get_desktop(0);
    if (!desktop) { atspi_exit(); return result; }
    QSet<AtspiAccessible *> visited;
    int budget = 160;
    std::function<void(AtspiAccessible *, int)> inspect = [&](AtspiAccessible *object, int depth) {
        if (!object || depth > 12 || --budget < 0 || timer.elapsed() > 700 || visited.contains(object)) return;
        visited.insert(object);
        GError *error = nullptr;
        const auto pid = depth > 0 ? atspi_accessible_get_process_id(object, &error) : 0;
        if (error) { g_clear_error(&error); return; }
        if (excluded > 0 && pid == excluded) return;
        auto *component = atspi_accessible_get_component_iface(object);
        if (component) {
            auto *states = atspi_accessible_get_state_set(object);
            const bool showing = states && atspi_state_set_contains(states, ATSPI_STATE_SHOWING);
            if (states) g_object_unref(states);
            auto *extents = showing ? atspi_component_get_extents(component, ATSPI_COORD_TYPE_SCREEN, &error) : nullptr;
            QRect bounds;
            if (extents) { bounds = QRect(extents->x, extents->y, extents->width, extents->height); g_free(extents); }
            g_object_unref(component);
            if (error) { g_clear_error(&error); return; }
            if (!bounds.contains(point)) return;
            auto *name = atspi_accessible_get_name(object, nullptr);
            auto target = manualTarget();
            target["source"] = "accessibility"; target["method"] = "atspi";
            target["label"] = QString::fromUtf8(name ? name : "").left(1000);
            g_free(name);
            result.append({bounds, target});
        }
        const int children = std::min(64, atspi_accessible_get_child_count(object, nullptr));
        for (int index = 0; index < children && budget > 0 && timer.elapsed() < 700; ++index) {
            auto *child = atspi_accessible_get_child_at_index(object, index, nullptr);
            inspect(child, depth + 1);
            if (child) g_object_unref(child);
        }
    };
    inspect(desktop, 0);
    g_object_unref(desktop);
    atspi_exit();
    return result;
}
}
