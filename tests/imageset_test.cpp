#include "imageset.h"
#include "editor.h"
#include "ui.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
using namespace h2d;
namespace {
struct TestDirectory {
    QString path = QDir::current().absoluteFilePath("build-image-set/test-" + uniqueId());
    TestDirectory() { if (!QDir().mkpath(path)) throw std::runtime_error("Cannot create test directory"); }
    QString filePath(const QString &name) const { return QDir(path).filePath(name); }
};
ImageSet sample() {
    QImage image(320, 180, QImage::Format_ARGB32); image.fill(QColor("#24364c"));
    ImageSet set; set.name = "Desktop project";
    set.pages = {{"home", "Home", fromImage(image, "file", "Home")}, {"settings", "Settings", fromImage(image, "file", "Settings")}};
    Note note; note.isGlobal = true; note.comment = "Use a larger title";
    set.pages[1].document.notes.append(note);
    return set;
}
}
class ImageSetTests : public QObject {
    Q_OBJECT
  private slots:
    void feedbackOnlyIncludesAnnotatedPages() {
        const auto value = QJsonDocument::fromJson(serializeImageSetFeedback(sample())).object();
        QCOMPARE(value["schemaVersion"].toString(), QString(ImageSetFeedbackVersion));
        QCOMPARE(value["pages"].toArray().size(), 1);
        QCOMPARE(value["pages"].toArray()[0].toObject()["id"].toString(), QString("settings"));
        QCOMPARE(value["pages"].toArray()[0].toObject()["index"].toInt(), 2);
        QVERIFY(value["pages"].toArray()[0].toObject()["feedback"].toObject().contains("image"));
        QVERIFY(!value["pageIndex"].toArray()[0].toObject().contains("imageFile"));
        QVERIFY(!value["pageIndex"].toArray()[0].toObject().contains("image"));
    }
    void projectRoundTripKeepsEveryPageAndGroupNotes() {
        try {
        TestDirectory dir;
        auto set = sample(); set.groupNotes.append(QJsonObject{{"text", "Consistent spacing"}});
        saveBytes(dir.filePath("project.edithere"), serializeImageSet(set));
        QVERIFY(isImageSetFile(dir.filePath("project.edithere")));
        const auto restored = loadImageSet(dir.filePath("project.edithere"));
        QCOMPARE(restored.pages.size(), 2);
        QCOMPARE(restored.pages[1].id, QString("settings"));
        QCOMPARE(restored.pages[1].document.notes[0].comment, QString("Use a larger title"));
        QCOMPARE(imageSetAnnotationCount(restored), 2);
        } catch (const std::exception &error) { QFAIL(error.what()); }
    }
    void manifestReplacementKeepsPageIdentityAndRejectsStaleHash() {
        TestDirectory dir; auto set = sample();
        const auto image = dir.filePath("home.png"); saveBytes(image, set.pages[0].document.png);
        const auto manifest = dir.filePath("set.json");
        QJsonObject value{{"schemaVersion", ImageSetVersion}, {"name", "Project"}, {"pages", QJsonArray{QJsonObject{{"id", "home"}, {"name", "Home"}, {"path", "home.png"}}}}};
        saveBytes(manifest, QJsonDocument(value).toJson());
        QCOMPARE(loadImageSet(manifest).pages[0].id, QString("home"));
        QImage next(200, 100, QImage::Format_ARGB32); next.fill(Qt::red); saveBytes(image, encodePng(next));
        const auto updated = loadImageSet(manifest);
        QCOMPARE(updated.pages[0].id, QString("home")); QCOMPARE(updated.pages[0].document.image.size(), QSize(200, 100));
        auto page = value["pages"].toArray()[0].toObject(); page["sha256"] = QString(64, '0'); value["pages"] = QJsonArray{page};
        saveBytes(manifest, QJsonDocument(value).toJson());
        QVERIFY_EXCEPTION_THROWN(loadImageSet(manifest), std::runtime_error);
    }
    void switchingPagesPreservesEditsAndSubmittedReviewCanBeReplaced() {
        Editor editor; editor.setImageSet(sample());
        auto tabs = editor.findChild<QTabBar *>("imageSetTabs"); QVERIFY(tabs); QCOMPARE(tabs->count(), 2);
        QCOMPARE(editor.document().notes.size(), 0);
        tabs->setCurrentIndex(1); QCOMPARE(editor.document().notes.size(), 1);
        auto &doc = const_cast<Document &>(editor.document()); doc.notes[0].comment = "Edited settings title"; doc.dirty = true;
        tabs->setCurrentIndex(0); tabs->setCurrentIndex(1);
        QCOMPARE(editor.document().notes[0].comment, QString("Edited settings title"));
        QVERIFY(editor.hasUnsavedChanges());
        const auto feedback = QJsonDocument::fromJson(editor.agentFeedback(true)).object();
        QCOMPARE(feedback["pages"].toArray().size(), 1);
        editor.markFeedbackSubmitted(); QVERIFY(!editor.hasUnsavedChanges());
        editor.setDocument(sample().pages[0].document); QVERIFY(!editor.hasImageSet()); QVERIFY(tabs->parentWidget()->isHidden());
    }
    void duplicateIdsFailAndGroupOnlyFeedbackDoesNotAttachImages() {
        auto set = sample(); set.pages[1].id = "home";
        QVERIFY_EXCEPTION_THROWN(serializeImageSet(set), std::runtime_error);
        set = sample(); set.pages[1].document.notes.clear(); set.groupNotes.append(QJsonObject{{"text", "Use dark mode"}});
        const auto value = QJsonDocument::fromJson(serializeImageSetFeedback(set)).object();
        QCOMPARE(value["pages"].toArray().size(), 0); QCOMPARE(value["groupNotes"].toArray().size(), 1);
    }
    void rendersProjectReview() {
        applyTheme();
        auto set = sample();
        const auto manifest = qEnvironmentVariable("EDITHERE_SET_VISUAL_MANIFEST");
        if (!manifest.isEmpty()) set = loadImageSet(manifest);
        Editor editor; editor.setImageSet(set);
        auto tabs = editor.findChild<QTabBar *>("imageSetTabs");
        QVERIFY(tabs); tabs->setCurrentIndex(1);
        QTest::qWait(100);
        const auto image = editor.grab();
        QVERIFY(!image.isNull()); QVERIFY(image.width() >= 600);
        const auto output = qEnvironmentVariable("EDITHERE_SET_VISUAL_OUTPUT");
        if (!output.isEmpty()) QVERIFY(image.save(output));
    }
};
QTEST_MAIN(ImageSetTests)
#include "imageset_test.moc"
