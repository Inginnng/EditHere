#pragma once

#include "b2assetserver.h"
#include "imageset.h"
#include "ocr.h"
#include "settings.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QVector>

namespace h2d {

class WebView2Host;

struct B2Page {
    QString id;
    QString name;
    QString routeHint;
    Document document;
    QString mediaUrl;
};

// Native desktop shell window hosting the unified React/Radix B2 workbench
// inside Microsoft Edge WebView2 (Windows), backed directly by EditHere's C++20
// capture, block-detection, Windows OCR, PinWindow, and Agent feedback core.
class B2Window final : public QMainWindow {
    Q_OBJECT
  public:
    explicit B2Window(const AppSettings &settings, const QString &settingsFile = {}, QWidget *parent = nullptr);
    ~B2Window() override;

    void openCapturedDocument(Document doc);
    void openMediaPath(const QString &path);
    void setPageContext(const QString &pageContext);
    void setAgentSession(bool active, const QString &outputPath = {},
                         const FeedbackExportOptions &options = {});
    void setPreferences(const AppSettings &settings);
    void navigateToView(const QString &viewId);
    void triggerOpenDialog();

    bool hasDocument() const { return !pages_.isEmpty(); }
    bool hasImageSet() const { return isImageSetProject_ || pages_.size() > 1; }
    bool hasUnsavedChanges() const { return dirty_; }
    bool isAgentSessionActive() const { return agentSession_; }
    void markFeedbackSubmitted() { dirty_ = false; }

  signals:
    void captureRequested();
    void pinImageRequested(const QImage &image);
    void preferencesUpdated(const AppSettings &settings);
    void agentSessionCompleted(const QString &outputPath, int annotationCount,
                               const QString &schemaVersion, const QString &imageFile,
                               const QString &previewFile);
    void agentSessionCancelled();
    void webviewFailed(const QString &reason);

  protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    void handleWebMessage(const QJsonObject &message);
    void sendRpcResult(const QJsonValue &id, bool ok, const QJsonValue &data = {},
                       const QString &error = {});
    void pushEvent(const QString &eventName, const QJsonObject &data);
    void pushSessionState();

    QJsonObject buildSessionStateJson() const;
    QJsonObject buildDoctorSnapshotJson() const;
    QJsonObject buildPreferencesJson() const;
    QJsonArray buildPagesJson() const;
    QJsonArray blocksForPage(const B2Page &page) const;
    void syncDwmTitleBar(bool dark);
    bool isEffectiveDark() const;

    Document buildDocumentFromJsPage(const B2Page &basePage, const QJsonObject &jsPage) const;
    ImageSet buildImageSetFromPayload(const QJsonObject &payload) const;
    void buildBundleFromPayload(const QJsonObject &payload, const FeedbackExportOptions &options,
                                FeedbackBundle *outBundle, int *outCount, QString *outSchema) const;
    void handleOpenFilesDialog(const QJsonValue &rpcId);
    void handleSaveTextDialog(const QJsonValue &rpcId, const QJsonObject &params);
    void handleUpdatePageImage(const QJsonValue &rpcId, const QJsonObject &params);
    void handleCopyImage(const QJsonValue &rpcId, const QJsonObject &params);
    void handleSaveImageDialog(const QJsonValue &rpcId, const QJsonObject &params);
    void handleSaveProjectDialog(const QJsonValue &rpcId, const QJsonObject &params);
    void handleCopyFeedbackBundle(const QJsonValue &rpcId, const QJsonObject &params);
    void handleOcrRect(const QJsonValue &rpcId, const QJsonObject &params);
    void handleAgentComplete(const QJsonValue &rpcId, const QJsonObject &params);

    B2AssetServer server_;
    WebView2Host *webview_ = nullptr;
    OcrEngine *ocr_ = nullptr;
    AppSettings settings_;
    QString settingsFile_;

    QVector<B2Page> pages_;
    QString activePageId_;
    QString setTitle_;
    QString pageContext_;
    QJsonArray groupNotes_;
    bool isImageSetProject_ = false;

    bool agentSession_ = false;
    QString agentOutput_;
    FeedbackExportOptions agentExportOptions_;
    bool jsReady_ = false;
    bool dirty_ = false;
    bool uiDarkOverride_ = false;
    bool hasDarkOverride_ = false;
};

} // namespace h2d
