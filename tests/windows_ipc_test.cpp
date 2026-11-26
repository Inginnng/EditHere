#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <QTest>

namespace {
DWORD integrityRid = SECURITY_MANDATORY_MEDIUM_RID;
bool elevated = false;

BOOL WINAPI tokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS kind, LPVOID buffer,
                             DWORD capacity, PDWORD written) {
    if (kind == TokenElevation) {
        if (capacity < sizeof(TOKEN_ELEVATION)) return FALSE;
        static_cast<TOKEN_ELEVATION *>(buffer)->TokenIsElevated = elevated;
        *written = sizeof(TOKEN_ELEVATION);
        return TRUE;
    }
    if (kind != TokenIntegrityLevel)
        return GetTokenInformation(token, kind, buffer, capacity, written);
    *written = sizeof(TOKEN_MANDATORY_LABEL) + GetSidLengthRequired(1);
    if (capacity < *written) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    const auto label = static_cast<TOKEN_MANDATORY_LABEL *>(buffer);
    const auto sid = reinterpret_cast<SID *>(static_cast<BYTE *>(buffer) + sizeof(*label));
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    InitializeSid(sid, &authority, 1);
    *GetSidSubAuthority(sid, 0) = integrityRid;
    label->Label = {sid, SE_GROUP_INTEGRITY};
    return TRUE;
}
}

// Supply each privilege combination independently of the test runner's token.
#define GetTokenInformation tokenInformation
#include "../app/localauth.cpp"
#undef GetTokenInformation

class WindowsIpcTests : public QObject {
    Q_OBJECT
  private slots:
    void tokenPolicy_data() {
        QTest::addColumn<quint32>("rid");
        QTest::addColumn<bool>("isElevated");
        QTest::newRow("low") << quint32(SECURITY_MANDATORY_LOW_RID) << false;
        QTest::newRow("medium") << quint32(SECURITY_MANDATORY_MEDIUM_RID) << false;
        QTest::newRow("medium-plus") << quint32(SECURITY_MANDATORY_MEDIUM_PLUS_RID) << false;
        QTest::newRow("high") << quint32(SECURITY_MANDATORY_HIGH_RID) << false;
        QTest::newRow("system") << quint32(SECURITY_MANDATORY_SYSTEM_RID) << false;
        QTest::newRow("elevated") << quint32(SECURITY_MANDATORY_MEDIUM_RID) << true;
    }
    void tokenPolicy() {
        QFETCH(quint32, rid);
        QFETCH(bool, isElevated);
        integrityRid = rid;
        elevated = isElevated;
        QCOMPARE(h2d::ipcServerPolicyError(), QString());
    }
};
QTEST_GUILESS_MAIN(WindowsIpcTests)
#include "windows_ipc_test.moc"
