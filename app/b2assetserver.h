#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QUrl>

class QTcpServer;
class QTcpSocket;

namespace h2d {

// Serves the B2 frontend bundle (`frontend/dist`) and in-memory lossless
// screenshots over a strict loopback HTTP server for WebView2 / WKWebView /
// WebKitGTK hosts without copying images to disk or exposing external origins.
class B2AssetServer final : public QObject {
    Q_OBJECT
  public:
    explicit B2AssetServer(QObject *parent = nullptr);
    ~B2AssetServer() override;

    bool start(QString *error = nullptr);
    bool isRunning() const;
    quint16 port() const;
    QString distDir() const { return distDir_; }
    QUrl indexUrl() const;

    QString registerImage(const QString &pageId, const QImage &image, const QByteArray &preencodedPng = {});
    void clearImages();

  private:
    void handleConnection();
    void processSocket(QTcpSocket *socket);
    static QString resolveDistDir();
    static QByteArray mimeTypeForPath(const QString &path);
    static void sendResponse(QTcpSocket *socket, int status, const QByteArray &reason,
                             const QByteArray &contentType, const QByteArray &body,
                             bool headOnly = false);

    QTcpServer *server_ = nullptr;
    QString distDir_;
    QString token_;
    QHash<QString, QByteArray> images_;
    quint64 mediaVersion_ = 0;
};

} // namespace h2d
