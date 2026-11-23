#include "b2assetserver.h"
#include "diagnostics.h"
#include "model.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

namespace h2d {

namespace {
QByteArray makeHexToken() {
    QByteArray raw;
    raw.reserve(16);
    for (int i = 0; i < 4; ++i) {
        const quint32 val = QRandomGenerator::system()->generate();
        raw.append(static_cast<char>((val >> 24) & 0xff));
        raw.append(static_cast<char>((val >> 16) & 0xff));
        raw.append(static_cast<char>((val >> 8) & 0xff));
        raw.append(static_cast<char>(val & 0xff));
    }
    return raw.toHex();
}
} // namespace

B2AssetServer::B2AssetServer(QObject *parent)
    : QObject(parent), server_(new QTcpServer(this)), token_(QString::fromLatin1(makeHexToken())) {
    distDir_ = resolveDistDir();
    connect(server_, &QTcpServer::newConnection, this, &B2AssetServer::handleConnection);
}

B2AssetServer::~B2AssetServer() {
    if (server_) server_->close();
}

QString B2AssetServer::resolveDistDir() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString deployed = QDir(exeDir).filePath("b2-ui");
    if (QFileInfo::exists(QDir(deployed).filePath("index.html"))) {
        return QFileInfo(deployed).canonicalFilePath();
    }
#ifdef EDITHERE_FRONTEND_DIST_DIR
    const QString devFallback = QString::fromUtf8(EDITHERE_FRONTEND_DIST_DIR);
    if (QFileInfo::exists(QDir(devFallback).filePath("index.html"))) {
        return QFileInfo(devFallback).canonicalFilePath();
    }
#endif
    return deployed;
}

bool B2AssetServer::start(QString *error) {
    if (server_->isListening()) return true;
    distDir_ = resolveDistDir();
    if (!QFileInfo::exists(QDir(distDir_).filePath("index.html"))) {
        const QString msg = QString("B2 frontend dist missing at %1").arg(distDir_);
        if (error) *error = msg;
        diagnostics::write(diagnostics::Level::Error, "b2.server_missing_dist", msg);
        return false;
    }
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        if (error) *error = server_->errorString();
        diagnostics::write(diagnostics::Level::Error, "b2.server_listen_failed", server_->errorString());
        return false;
    }
    diagnostics::write(diagnostics::Level::Info, "b2.server_started",
                       "B2 loopback asset server started",
                       {{"port", int(server_->serverPort())}, {"distDir", distDir_}});
    return true;
}

bool B2AssetServer::isRunning() const {
    return server_ && server_->isListening();
}

quint16 B2AssetServer::port() const {
    return server_ ? server_->serverPort() : 0;
}

QUrl B2AssetServer::indexUrl() const {
    if (!isRunning()) return {};
    return QUrl(QString("http://127.0.0.1:%1/index.html").arg(server_->serverPort()));
}

QString B2AssetServer::registerImage(const QString &pageId, const QImage &image) {
    if (!isRunning() || pageId.isEmpty() || image.isNull()) return {};
    images_[pageId] = encodePng(image);
    const quint64 v = ++mediaVersion_;
    const QByteArray encodedId = QUrl::toPercentEncoding(pageId);
    return QString("http://127.0.0.1:%1/media/%2/%3.png?v=%4")
        .arg(server_->serverPort())
        .arg(token_)
        .arg(QString::fromLatin1(encodedId))
        .arg(v);
}

void B2AssetServer::clearImages() {
    images_.clear();
}

void B2AssetServer::handleConnection() {
    while (QTcpSocket *socket = server_->nextPendingConnection()) {
        socket->setProperty("reqBuffer", QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { processSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void B2AssetServer::processSocket(QTcpSocket *socket) {
    QByteArray buf = socket->property("reqBuffer").toByteArray();
    buf.append(socket->readAll());
    if (buf.size() > 65536) {
        sendResponse(socket, 400, "Bad Request", "text/plain; charset=utf-8", "Request too large");
        return;
    }
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        socket->setProperty("reqBuffer", buf);
        return;
    }
    const QByteArray head = buf.left(headerEnd);
    const int lineEnd = head.indexOf("\r\n");
    const QList<QByteArray> parts = (lineEnd >= 0 ? head.left(lineEnd) : head).split(' ');
    if (parts.size() < 2) {
        sendResponse(socket, 400, "Bad Request", "text/plain; charset=utf-8", "Malformed request");
        return;
    }
    const QByteArray method = parts[0].toUpper();
    if (method != "GET" && method != "HEAD") {
        sendResponse(socket, 405, "Method Not Allowed", "text/plain; charset=utf-8", "Method Not Allowed");
        return;
    }
    const bool headOnly = (method == "HEAD");

    QString target = QString::fromUtf8(parts[1]);
    const int qIdx = target.indexOf('?');
    if (qIdx >= 0) target = target.left(qIdx);
    if (target.isEmpty() || target == "/") target = "/index.html";

    // In-memory lossless media route: /media/<token>/<pageId>.png
    if (target.startsWith("/media/")) {
        const QString rest = target.mid(7);
        const int slash = rest.indexOf('/');
        if (slash <= 0) {
            sendResponse(socket, 404, "Not Found", "text/plain", "Not Found", headOnly);
            return;
        }
        const QString reqToken = rest.left(slash);
        if (reqToken != token_) {
            sendResponse(socket, 403, "Forbidden", "text/plain", "Forbidden", headOnly);
            return;
        }
        QString encodedName = rest.mid(slash + 1);
        if (!encodedName.endsWith(".png", Qt::CaseInsensitive)) {
            sendResponse(socket, 404, "Not Found", "text/plain", "Not Found", headOnly);
            return;
        }
        encodedName.chop(4);
        const QString pageId = QUrl::fromPercentEncoding(encodedName.toUtf8());
        if (!images_.contains(pageId)) {
            sendResponse(socket, 404, "Not Found", "text/plain", "Image not found", headOnly);
            return;
        }
        sendResponse(socket, 200, "OK", "image/png", images_.value(pageId), headOnly);
        return;
    }

    // Static frontend asset route inside distDir_
    QString rel = target;
    while (rel.startsWith('/')) rel.remove(0, 1);
    rel.replace('\\', '/');
    if (rel.isEmpty() || rel.contains("..")) {
        sendResponse(socket, 404, "Not Found", "text/plain", "Not Found", headOnly);
        return;
    }
    const QFileInfo distRootInfo(distDir_);
    const QString canonicalRoot = distRootInfo.canonicalFilePath();
    const QFileInfo fileInfo(QDir(canonicalRoot).filePath(rel));
    const QString canonicalFile = fileInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonicalFile.isEmpty() || !fileInfo.isFile() ||
        (!canonicalFile.startsWith(canonicalRoot + '/') && canonicalFile != canonicalRoot)) {
        sendResponse(socket, 404, "Not Found", "text/plain", "Not Found", headOnly);
        return;
    }

    QFile file(canonicalFile);
    if (!file.open(QIODevice::ReadOnly)) {
        sendResponse(socket, 500, "Internal Server Error", "text/plain", "Read error", headOnly);
        return;
    }
    const QByteArray bytes = file.readAll();
    sendResponse(socket, 200, "OK", mimeTypeForPath(canonicalFile), bytes, headOnly);
}

QByteArray B2AssetServer::mimeTypeForPath(const QString &path) {
    const QString lower = path.toLower();
    if (lower.endsWith(".html") || lower.endsWith(".htm")) return "text/html; charset=utf-8";
    if (lower.endsWith(".js") || lower.endsWith(".mjs")) return "application/javascript; charset=utf-8";
    if (lower.endsWith(".css")) return "text/css; charset=utf-8";
    if (lower.endsWith(".svg")) return "image/svg+xml";
    if (lower.endsWith(".png")) return "image/png";
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
    if (lower.endsWith(".webp")) return "image/webp";
    if (lower.endsWith(".json")) return "application/json; charset=utf-8";
    if (lower.endsWith(".txt")) return "text/plain; charset=utf-8";
    if (lower.endsWith(".woff2")) return "font/woff2";
    return "application/octet-stream";
}

void B2AssetServer::sendResponse(QTcpSocket *socket, int status, const QByteArray &reason,
                                 const QByteArray &contentType, const QByteArray &body,
                                 bool headOnly) {
    QByteArray resp;
    resp.append("HTTP/1.1 ");
    resp.append(QByteArray::number(status));
    resp.append(' ');
    resp.append(reason);
    resp.append("\r\nContent-Type: ");
    resp.append(contentType);
    resp.append("\r\nContent-Length: ");
    resp.append(QByteArray::number(body.size()));
    resp.append("\r\nCache-Control: no-store");
    resp.append("\r\nAccess-Control-Allow-Origin: *");
    resp.append("\r\nConnection: close\r\n\r\n");
    if (!headOnly && !body.isEmpty()) resp.append(body);
    socket->write(resp);
    socket->flush();
    socket->disconnectFromHost();
}

} // namespace h2d
