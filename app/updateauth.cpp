#include "updateauth.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
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
    WINTRUST_SIGNATURE_SETTINGS signature{};
    signature.cbStruct = sizeof(signature);
    // Verify the primary embedded signature specifically, then inspect its
    // verified signer. An unrelated secondary signature cannot supply the pin.
    signature.dwFlags = WSS_VERIFY_SPECIFIC;
    signature.dwIndex = 0;
    WINTRUST_DATA trust{};
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
    const LONG status = WinVerifyTrust(noInteractiveWindow, &policy, &trust);
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
    WinVerifyTrust(noInteractiveWindow, &policy, &trust);
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
