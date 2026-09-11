#include "updatechecker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTest>
using namespace h2d;
class UpdateTests : public QObject {
    Q_OBJECT
    QByteArray release(const QString &version) {
        return QJsonDocument(QJsonObject{{"tag_name", version},
                                         {"draft", false},
                                         {"prerelease", false},
                                         {"html_url",
                                          "https://github.com/Inginnng/HelpDesign/releases/tag/" + version}})
            .toJson();
    }
  private slots:
    void httpsBackendAvailable() {
        QVERIFY2(QSslSocket::supportsSsl(), "HTTPS support must be included in the portable app");
    }
    void numericOrdering_data() {
        QTest::addColumn<QString>("remote");
        QTest::addColumn<QString>("local");
        QTest::addColumn<int>("status");
        QTest::newRow("patch-ten") << "v0.8.10" << "0.8.9" << int(UpdateChecker::Available);
        QTest::newRow("same") << "v0.8.3" << "0.8.3" << int(UpdateChecker::Current);
        QTest::newRow("development") << "v0.8.2" << "0.8.3" << int(UpdateChecker::NewerLocal);
        QTest::newRow("minor") << "0.9.0" << "0.8.99" << int(UpdateChecker::Available);
        QTest::newRow("major") << "v1.0.0" << "0.99.99" << int(UpdateChecker::Available);
    }
    void numericOrdering() {
        QFETCH(QString, remote);
        QFETCH(QString, local);
        QFETCH(int, status);
        const auto result = UpdateChecker::parseRelease(release(remote), local);
        QCOMPARE(int(result.status), status);
        QVERIFY(result.url.path().endsWith(remote));
    }
    void rejectsUntrustedAndNonStableReleases() {
        auto base = QJsonDocument::fromJson(release("v0.8.3")).object();
        for (const auto &url : {"http://github.com/Inginnng/HelpDesign/releases/tag/v0.8.3",
                                "https://example.com/Inginnng/HelpDesign/releases/tag/v0.8.3",
                                "https://github.com/elsewhere/other/releases/tag/v0.8.3",
                                "https://github.com/Inginnng/HelpDesign/releases/tag/v0.8.3?x=1",
                                "https://user@github.com/Inginnng/HelpDesign/releases/tag/v0.8.3"}) {
            auto changed = base;
            changed["html_url"] = url;
            QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.8.2").status,
                     UpdateChecker::Failed);
        }
        for (const auto &key : {"draft", "prerelease"}) {
            auto changed = base;
            changed[key] = true;
            QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.8.2").status,
                     UpdateChecker::Failed);
            changed.remove(key);
            QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.8.2").status,
                     UpdateChecker::Failed);
        }
        for (const auto &version : {"v0.8.3-beta.1", "0.8", "unknown", "999999999999999999999.8.3"})
            QCOMPARE(UpdateChecker::parseRelease(release(version), "0.8.2").status, UpdateChecker::Failed);
        QCOMPARE(UpdateChecker::parseRelease(release("v0.8.3"), "invalid").status, UpdateChecker::Failed);
    }
    void malformedOrInaccessibleNeverMeansCurrent() {
        for (const auto &bytes : {QByteArray("{\"message\":\"Not Found\"}"), QByteArray("<html>error</html>"),
                                  QByteArray("[]"), QByteArray(), QByteArray(1024 * 1024 + 1, ' ')}) {
            const auto result = UpdateChecker::parseRelease(bytes, "0.8.3");
            QCOMPARE(result.status, UpdateChecker::Failed);
            QCOMPARE(result.url, UpdateChecker::releasesUrl());
        }
        UpdateChecker checker;
        QVERIFY(!checker.busy()); // Construction is offline; only explicit check starts a request.
    }
    void liveReleaseCheck() {
        if (!qEnvironmentVariableIsSet("H2D_LIVE_UPDATES"))
            QSKIP("Opt-in real GitHub release integration check");
        UpdateChecker checker;
        QSignalSpy result(&checker, &UpdateChecker::finished);
        checker.check();
        checker.check(); // Coalesce concurrent checks.
        QVERIFY(result.wait(20000));
        QCOMPARE(result.size(), 1);
        QVERIFY(!checker.busy());
        qInfo().noquote() << result.first()[1].toString();
        QVERIFY(result.first()[0].value<UpdateChecker::Status>() != UpdateChecker::Failed);
    }
};
QTEST_GUILESS_MAIN(UpdateTests)
#include "update_test.moc"
