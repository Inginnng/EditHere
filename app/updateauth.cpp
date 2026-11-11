#include "updateauth.h"
#ifdef EDITHERE_UPDATE_TEST_TRUST
#include "../tests/fixtures/update-signature-public.h"
#else
#include "updatekey.h"
#endif
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <bcrypt.h>
#endif

#ifdef Q_OS_WIN
#ifdef __MINGW32__
// MinGW headers (used for the packaged Windows build) predate the Win8
// signature-settings API used to pin the primary embedded signer. Mirror the
// Microsoft SDK declarations under project-private names: WinVerifyTrust reads
// cbStruct, so the wider WINTRUST_DATA tail is accepted by the system provider.
#ifndef WSS_VERIFY_SPECIFIC
#define WSS_VERIFY_SPECIFIC 0x00000001
#endif
#ifndef WTD_DISABLE_MD2_MD4
#define WTD_DISABLE_MD2_MD4 0x10000000
#endif
#ifndef WTD_MOTW
#define WTD_MOTW 0x20000000
#endif
typedef struct _H2D_WINTRUST_SIGNATURE_SETTINGS {
  DWORD cbStruct;
  DWORD dwIndex;
  DWORD dwFlags;
  DWORD cSecondarySigs;
  DWORD dwVerifiedSigIndex;
  PCERT_STRONG_SIGN_PARA pCryptoPolicy;
} H2D_WINTRUST_SIGNATURE_SETTINGS;
// Field order mirrors the modern SDK's WINTRUST_DATA, whose MinGW counterpart
// lacks the final pSignatureSettings member.
typedef struct _H2D_WINTRUST_DATA {
  DWORD cbStruct;
  LPVOID pPolicyCallbackData;
  LPVOID pSIPClientData;
  DWORD dwUIChoice;
  DWORD fdwRevocationChecks;
  DWORD dwUnionChoice;
  union {
    PWINTRUST_FILE_INFO pFile;
    PWINTRUST_CATALOG_INFO pCatalog;
    PWINTRUST_BLOB_INFO pBlob;
    PWINTRUST_SGNR_INFO pSgnr;
    PWINTRUST_CERT_INFO pCert;
  };
  DWORD dwStateAction;
  HANDLE hWVTStateData;
  WCHAR *pwszURLReference;
  DWORD dwProvFlags;
  DWORD dwUIContext;
  H2D_WINTRUST_SIGNATURE_SETTINGS *pSignatureSettings;
} H2D_WINTRUST_DATA;
#endif
#endif

#ifndef EDITHERE_UPDATE_PUBLISHER_CERT_SHA256
#define EDITHERE_UPDATE_PUBLISHER_CERT_SHA256 ""
#endif

namespace h2d::updateauth {
namespace {
bool validPin(const QString &pin) {
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return pin.size() == 64 && pattern.match(pin).hasMatch();
}
} // namespace

QString publisherCertificateSha256() {
    return QStringLiteral(EDITHERE_UPDATE_PUBLISHER_CERT_SHA256);
}
bool publisherConfigured() {
#ifdef Q_OS_WIN
    return validPin(publisherCertificateSha256());
#else
    return false;
#endif
}
bool certificateMode() {
    // A configured but invalid pin also fails closed, never selects project mode.
    return !publisherCertificateSha256().isEmpty();
}
bool installationConfigured() {
#ifdef Q_OS_WIN
    static const QRegularExpression keyPattern(QStringLiteral("\\A[0-9a-f]{128}\\z"));
    return certificateMode() ? publisherConfigured()
                             : keyPattern.match(QString::fromLatin1(ProjectPublicKeyHex)).hasMatch();
#else
    return false;
#endif
}
bool canonicalVersion(const QString &version) {
    static const QRegularExpression pattern(QStringLiteral("\\A(0|[1-9][0-9]{0,9})\\.(0|[1-9][0-9]{0,9})\\.(0|[1-9][0-9]{0,9})\\z"));
    if (!pattern.match(version).hasMatch()) return false;
    for (const auto &part : version.split('.')) {
        bool ok = false;
        part.toInt(&ok);
        if (!ok) return false;
    }
    return true;
}
bool verifyProjectSignature(const QByteArray &metadata, const QString &version,
                            const QString &assetName, const QString &sha256, QString *error) {
    const auto fail = [error] {
        if (error) *error = QCoreApplication::translate("h2d::UpdateChecker",
            "更新签名无效或与本次安装包不一致，已停止更新。请前往发布页手动下载。");
        return false;
    };
    static const QRegularExpression hashPattern(QStringLiteral("\\A[0-9a-f]{64}\\z"));
    static const QRegularExpression keyPattern(QStringLiteral("\\A[0-9a-f]{128}\\z"));
    if (metadata.size() > 1024 || !canonicalVersion(version) || !hashPattern.match(sha256).hasMatch() ||
        (assetName != "EditHere-win-x64-setup.exe" &&
         assetName != "EditHere-" + version + "-win-x64-setup.exe") ||
        !keyPattern.match(QString::fromLatin1(ProjectPublicKeyHex)).hasMatch())
        return fail();
    const QByteArray payload = "EditHere Windows Update Signature v1\n" + version.toLatin1() + '\n' +
                               assetName.toLatin1() + '\n' + sha256.toLatin1() + '\n';
    if (!metadata.startsWith(payload)) return fail();
    const auto tail = metadata.mid(payload.size());
    // Exactly one canonical base64 P1363 r||s signature followed by LF. No JSON
    // duplicate fields, Unicode aliases, CRLF, trailing fields or implicit trim.
    if (tail.size() != 89 || !tail.endsWith('\n')) return fail();
    const auto encoded = tail.first(88);
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() != 64 || decoded.decoded.toBase64() != encoded) return fail();
#ifdef Q_OS_WIN
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0) return fail();
    BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32};
    QByteArray blob(reinterpret_cast<const char *>(&header), sizeof(header));
    blob += QByteArray::fromHex(ProjectPublicKeyHex);
    BCRYPT_KEY_HANDLE key = nullptr;
    const auto imported = BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
        reinterpret_cast<PUCHAR>(blob.data()), ULONG(blob.size()), 0);
    auto digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    auto signature = decoded.decoded;
    const bool valid = imported == 0 && BCryptVerifySignature(key, nullptr,
        reinterpret_cast<PUCHAR>(digest.data()), ULONG(digest.size()),
        reinterpret_cast<PUCHAR>(signature.data()), ULONG(signature.size()), 0) == 0;
    if (key) BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!valid) return fail();
    if (error) error->clear();
    return true;
#else
    return fail();
#endif
}
bool verifyPublisherSignature(const QString &filePath, const QString &certificateSha256,
                              QString *error) {
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!validPin(certificateSha256))
        return fail(QCoreApplication::translate("h2d::UpdateChecker", "未配置可信发布者，程序内安装已停用。请前往发布页手动下载。"));
#ifdef Q_OS_WIN
    const auto nativePath = QDir::toNativeSeparators(filePath);
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = reinterpret_cast<LPCWSTR>(nativePath.utf16());
#ifdef __MINGW32__
    H2D_WINTRUST_SIGNATURE_SETTINGS signature{};
#else
    WINTRUST_SIGNATURE_SETTINGS signature{};
#endif
    signature.cbStruct = sizeof(signature);
    // Verify the primary embedded signature specifically, then inspect its
    // verified signer. An unrelated secondary signature cannot supply the pin.
    signature.dwFlags = WSS_VERIFY_SPECIFIC;
    signature.dwIndex = 0;
#ifdef __MINGW32__
    H2D_WINTRUST_DATA trust{};
#else
    WINTRUST_DATA trust{};
#endif
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | WTD_DISABLE_MD2_MD4 | WTD_MOTW;
    trust.pSignatureSettings = &signature;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const HWND noInteractiveWindow = reinterpret_cast<HWND>(INVALID_HANDLE_VALUE);
    // WinVerifyTrust returns a LONG status; only zero denotes success.
    const LONG status = WinVerifyTrust(noInteractiveWindow, &policy,
                                       reinterpret_cast<WINTRUST_DATA *>(&trust));
    QString signerHash;
    if (status == ERROR_SUCCESS) {
        const auto provider = WTHelperProvDataFromStateData(trust.hWVTStateData);
        const auto signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        if (signer && signer->dwError == ERROR_SUCCESS && signer->csCertChain > 0 && signer->pasCertChain) {
            const auto certificate = signer->pasCertChain[0].pCert;
            if (certificate && certificate->pbCertEncoded && certificate->cbCertEncoded) {
                const auto encoded = QByteArray::fromRawData(
                    reinterpret_cast<const char *>(certificate->pbCertEncoded), certificate->cbCertEncoded);
                signerHash = QString::fromLatin1(QCryptographicHash::hash(encoded, QCryptographicHash::Sha256).toHex());
            }
        }
    }
    // Every VERIFY call, including failures, must release provider state.
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(noInteractiveWindow, &policy, reinterpret_cast<WINTRUST_DATA *>(&trust));
    if (status != ERROR_SUCCESS)
        return fail(QCoreApplication::translate("h2d::UpdateChecker", "安装包的发布者签名无效或无法验证，已停止更新。请前往发布页手动下载。"));
    if (signerHash.isEmpty() || signerHash.compare(certificateSha256, Qt::CaseInsensitive) != 0)
        return fail(QCoreApplication::translate("h2d::UpdateChecker", "安装包签名与内置的可信发布者不一致，已停止更新。请前往发布页手动下载。"));
    if (error) error->clear();
    return true;
#else
    Q_UNUSED(filePath);
    return fail(QCoreApplication::translate("h2d::UpdateChecker", "未找到适合当前系统的更新包，请前往发布页手动下载。"));
#endif
}
} // namespace h2d::updateauth
