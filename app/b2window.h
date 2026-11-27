#pragma once

#include "aiconfig.h"
#include "aicredentials.h"
#include "aifeedback.h"
#include "b2assetserver.h"
#include "imageset.h"
#include "ocr.h"
#include "settings.h"
#include "updatechecker.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <functional>

namespace h2d {

class WebView2Host;

struct B2Page {
    QString id;
    QString name;
    QString routeHint;
    Document document;
    QString mediaUrl;
    bool blocksDetected = false;
};

// Native desktop shell window hosting the unified React/Radix B2 workbench
// inside Microsoft Edge WebView2 (Windows), backed directly by EditHere's C++20
// capture, block-detection, Windows OCR, pinned image windows, and Agent feedback core.
class B2Window final : public QMainWindow {
    Q_OBJECT
  public:
    explicit B2Window(const AppSettings &settings, const QString &settingsFile = {}, QWidget *parent = nullptr);
    ~B2Window() override;

    void openCapturedDocument(Document doc, bool replaceExisting = false);
    bool openMediaPath(const QString &path);
    bool allowReplace();
    void openEmpty();
    void setPageContext(const QString &pageContext);
    void setAgentSession(bool active, const QString &outputPath = {},
                         const FeedbackExportOptions &options = {});
    void setPreferences(const AppSettings &settings);
    using SettingsApplyHandler = std::function<QString(const AppSettings &)>;
    void setSettingsApplyHandler(SettingsApplyHandler handler) { settingsApplyHandler_ = std::move(handler); }
    void navigateToView(const QString &viewId);
    void offerUpdate(const UpdateChecker::Result &result);
    void setUpdatePreparationHandler(std::function<bool()> handler) {
        updater_->setInstallConfirmation(std::move(handler));
    }
    void setUpdateExitConfirmation(std::function<bool()> handler) {
        updater_->setExitConfirmation(std::move(handler));
    }
    void trackWorkspaceChanges();
    void triggerOpenDialog();

    bool hasDocument() const { return !pages_.isEmpty(); }
    bool hasImageSet() const { return isImageSetProject_ || pages_.size() > 1; }
    bool hasUnsavedChanges() const { return dirty_; }
    bool startsNewWorkspaceOnCapture() const { return isImageSetProject_; }
    bool isAgentSessionActive() const { return agentSession_; }

  signals:
    void captureRequested(bool append);
    void pinImageRequested(const QImage &image);
    void preferencesUpdated(const AppSettings &settings);
    void agentSessionCompleted(const QString &outputPath, int annotationCount,
                               const QString &schemaVersion, const QString &imageFile,
                               const QString &previewFile);
    void agentSessionCancelled();
    void webviewFailed(const QString &reason);
    void workspaceSnapshotReceived();
    void workspaceChanged();
    void updateInstallStarted();

  protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    void handleWebMessage(const QJsonObject &message);
    void sendRpcResult(const QJsonValue &id, bool ok, const QJsonValue &data = {},
                       const QString &error = {});
    void pushEvent(const QString &eventName, const QJsonObject &data);
    void pushSessionState();
    void resetWorkspace();
    void appendCapturedDocument(Document doc);
    QString uniquePageName(const QString &baseName) const;
    void markWorkspaceDirty();
    QJsonObject requestWorkspaceSnapshot();
    QJsonObject updateResultJson() const;

    QJsonObject buildSessionStateJson() const;
    QJsonObject buildDoctorSnapshotJson() const;
    QJsonObject buildDiagnosticsJson() const;
    QJsonObject buildPreferencesJson() const;
    QJsonObject buildAiStateJson() const;
    QJsonObject buildSkillsStateJson() const;
    QJsonArray buildPagesJson() const;
    QJsonArray blocksForPage(const B2Page &page) const;
    void syncDwmTitleBar(bool dark);
    bool isEffectiveDark() const;
    void preloadAiCredentials();
    void runStartupSkillCheckIfNeeded();
    void resetBlockDetectionQueue();
    void enqueueBlockDetection(const QStringList &pageIds, bool prioritizeActive = false);
    void pumpBlockDetectionQueue();

    Document buildDocumentFromJsPage(const B2Page &basePage, const QJsonObject &jsPage) const;
    ImageSet buildImageSetFromPayload(const QJsonObject &payload) const;
    void buildBundleFromPayload(const QJsonObject &payload, const FeedbackExportOptions &options,
                                FeedbackBundle *outBundle, int *outCount, QString *outSchema) const;
    void handleOpenFilesDialog(const QJsonValue &rpcId);
    void handleSaveTextDialog(const QJsonValue &rpcId, const QJsonObject &params);
    void handleUpdatePageImage(const QJsonValue &rpcId, const QJsonObject &params);
    void handleCopyImage(const QJsonValue &rpcId, const QJsonObject &params);
    void handleSaveImageDialog(const QJsonValue &rpcId, const QJsonObject &params);
    QString saveProject(const QJsonObject &payload, const QString &suggested);
    void handleSaveProjectDialog(const QJsonValue &rpcId, const QJsonObject &params);
    void handleCopyFeedbackBundle(const QJsonValue &rpcId, const QJsonObject &params);
    void handleSaveSettings(const QJsonValue &rpcId, const QJsonObject &params);
    void handleDiagnosticsExport(const QJsonValue &rpcId);
    void handleDiagnosticsOpenFolder(const QJsonValue &rpcId);
    void handleUpdatesCheck(const QJsonValue &rpcId);
    void handleSkillsSync(const QJsonValue &rpcId, const QJsonObject &params);
    void handleOcrRect(const QJsonValue &rpcId, const QJsonObject &params);
    void handleAgentComplete(const QJsonValue &rpcId, const QJsonObject &params);
    void handleAiSaveProviders(const QJsonValue &rpcId, const QJsonObject &params);
    void handleAiTestConnection(const QJsonValue &rpcId, const QJsonObject &params);
    void handleAiAnalyze(const QJsonValue &rpcId, const QJsonObject &params);

    B2AssetServer server_;
    WebView2Host *webview_ = nullptr;
    OcrEngine *ocr_ = nullptr;
    AiCredentials *aiCredentials_ = nullptr;
    AiFeedbackClient *aiClient_ = nullptr;
    UpdateChecker *updater_ = nullptr;
    SettingsApplyHandler settingsApplyHandler_;
    AppSettings settings_;
    QString settingsFile_;
    QMap<QString, QString> apiKeys_;
    QJsonValue pendingAiTestId_;
    QJsonValue pendingAiAnalyzeId_;
    QJsonValue pendingUpdateCheckId_;
    std::optional<UpdateChecker::Result> updateResult_;
    QString pendingView_;
    QString snapshotRequest_;
    QJsonObject workspaceSnapshot_;
    qint64 lastInputTokens_ = 0;
    qint64 lastOutputTokens_ = 0;

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
    bool checkingReplacement_ = false;
    int workspaceGeneration_ = 0;
    bool savingProject_ = false;
    bool changedWhileSaving_ = false;
    bool uiDarkOverride_ = false;
    bool hasDarkOverride_ = false;
    bool startupSkillSyncChecked_ = false;

    quint64 detectGeneration_ = 0;
    QStringList detectQueue_;
    QSet<QString> detectPending_;
    bool detectRunning_ = false;
};

} // namespace h2d
