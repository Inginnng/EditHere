#include "webview2host.h"

namespace h2d {

class WebView2Host::Impl {};

bool WebView2Host::runtimeAvailable(QString *) {
    return false;
}

WebView2Host::WebView2Host(QWidget *parent)
    : QWidget(parent), impl_(new Impl()) {}

WebView2Host::~WebView2Host() {
    delete impl_;
}

void WebView2Host::initialize() {}

void WebView2Host::navigate(const QUrl &) {}

void WebView2Host::postJsonMessage(const QJsonObject &) {}

void WebView2Host::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); }
void WebView2Host::showEvent(QShowEvent *event) { QWidget::showEvent(event); }
void WebView2Host::moveEvent(QMoveEvent *event) { QWidget::moveEvent(event); }

} // namespace h2d
