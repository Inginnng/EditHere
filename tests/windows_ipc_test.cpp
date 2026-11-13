#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <QTest>

namespace {
enum class Reply { Valid, Failed, ShortHeader, LongResult, NullSid, BeforeSid, OutsideSid,
                   NoAuthorities, TooManyAuthorities, ShortSid, BadRevision, WrongAuthority };
Reply reply = Reply::Valid;
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
    if (reply == Reply::Failed || capacity < *written) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    const auto label = static_cast<TOKEN_MANDATORY_LABEL *>(buffer);
    const auto sid = reinterpret_cast<SID *>(static_cast<BYTE *>(buffer) + sizeof(*label));
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    InitializeSid(sid, &authority, 1);
    *GetSidSubAuthority(sid, 0) = integrityRid;
    label->Label = {sid, SE_GROUP_INTEGRITY};
    switch (reply) {
    case Reply::ShortHeader: *written = sizeof(*label) - 1; break;
    case Reply::LongResult: *written = capacity + 1; break;
    case Reply::NullSid: label->Label.Sid = nullptr; break;
    case Reply::BeforeSid: label->Label.Sid = buffer; break;
    case Reply::OutsideSid: label->Label.Sid = static_cast<BYTE *>(buffer) + capacity; break;
    case Reply::NoAuthorities: sid->SubAuthorityCount = 0; break;
    case Reply::TooManyAuthorities: sid->SubAuthorityCount = 255; break;
    case Reply::ShortSid: *written -= sizeof(DWORD); break;
    case Reply::BadRevision: sid->Revision = 0; break;
    case Reply::WrongAuthority: sid->IdentifierAuthority.Value[5] = 5; break;
    default: break;
    }
    return TRUE;
}
}

// Compile the production validator, substituting only the Win32 token query.
// Malformed replies cannot be produced with ordinary real process tokens.
#define GetTokenInformation tokenInformation
#include "../app/localauth.cpp"
#undef GetTokenInformation

class WindowsIpcTests : public QObject {
    Q_OBJECT
  private slots:
    void init() { reply = Reply::Valid; integrityRid = SECURITY_MANDATORY_MEDIUM_RID; elevated = false; }
    void tokenPolicy_data() {
        QTest::addColumn<quint32>("rid");
        QTest::addColumn<bool>("isElevated");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("low") << quint32(SECURITY_MANDATORY_LOW_RID) << false << true;
        QTest::newRow("medium") << quint32(SECURITY_MANDATORY_MEDIUM_RID) << false << true;
        QTest::newRow("medium-plus") << quint32(SECURITY_MANDATORY_MEDIUM_PLUS_RID) << false << true;
        QTest::newRow("high") << quint32(SECURITY_MANDATORY_HIGH_RID) << false << false;
        QTest::newRow("system") << quint32(SECURITY_MANDATORY_SYSTEM_RID) << false << false;
        QTest::newRow("elevated-flag") << quint32(SECURITY_MANDATORY_MEDIUM_RID) << true << false;
    }
    void tokenPolicy() {
        QFETCH(quint32, rid);
        QFETCH(bool, isElevated);
        QFETCH(bool, allowed);
        integrityRid = rid;
        elevated = isElevated;
        const auto error = h2d::ipcServerPolicyError();
        if (allowed) QVERIFY2(error.isEmpty(), qPrintable(error));
        else QVERIFY2(error.contains("elevated privileges"), qPrintable(error));
    }
    void malformedIntegrityIsRejectedAsUnreadable_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("query-failed") << int(Reply::Failed);
        QTest::newRow("short-header") << int(Reply::ShortHeader);
        QTest::newRow("oversized-result") << int(Reply::LongResult);
        QTest::newRow("null-sid") << int(Reply::NullSid);
        QTest::newRow("sid-in-header") << int(Reply::BeforeSid);
        QTest::newRow("sid-outside-buffer") << int(Reply::OutsideSid);
        QTest::newRow("no-subauthorities") << int(Reply::NoAuthorities);
        QTest::newRow("too-many-subauthorities") << int(Reply::TooManyAuthorities);
        QTest::newRow("truncated-sid") << int(Reply::ShortSid);
        QTest::newRow("invalid-revision") << int(Reply::BadRevision);
        QTest::newRow("wrong-authority") << int(Reply::WrongAuthority);
    }
    void malformedIntegrityIsRejectedAsUnreadable() {
        QFETCH(int, mode);
        reply = Reply(mode);
        QCOMPARE(h2d::ipcServerPolicyError(), QString("Unable to verify the process integrity level. IPC is disabled."));
    }
};
QTEST_GUILESS_MAIN(WindowsIpcTests)
#include "windows_ipc_test.moc"
