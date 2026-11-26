#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QWidget>

namespace h2d {

// Hosts a native Microsoft Edge WebView2 control (Windows) inside a QWidget
// without requiring any NuGet SDK or Electron dependency. Non-Windows builds
// link `webview2host_stub.cpp` until WKWebView / WebKitGTK hosts are added.
class WebView2Host final : public QWidget {
    Q_OBJECT
  public:
    static bool runtimeAvailable(QString *versionOut = nullptr);

    explicit WebView2Host(QWidget *parent = nullptr);
    ~WebView2Host() override;

    void initialize();
    void navigate(const QUrl &url);
    void postJsonMessage(const QJsonObject &message);
    bool isReady() const { return ready_; }
    QString runtimeVersion() const { return runtimeVersion_; }

  signals:
    void ready();
    void failed(const QString &reason);
    void messageReceived(const QJsonObject &message);

  protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

  private:
    class Impl;
    Impl *impl_ = nullptr;
    bool ready_ = false;
    QString runtimeVersion_;
};

} // namespace h2d
