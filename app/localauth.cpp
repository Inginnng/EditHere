#include "localauth.h"
#include "agentprotocol.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <cstring>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <cerrno>
#endif

namespace h2d {
namespace {
const QString unsafePath = "EditHere's IPC directory or credential is not private, owned by the current user, or is a symbolic link. IPC access was refused.";
#ifdef Q_OS_WIN
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
// TokenUser and TokenIntegrityLevel both contain a SID_AND_ATTRIBUTES followed
// by the SID. Keep that self-referencing result in one aligned, immovable buffer.
struct TokenSid {
    alignas(SID_AND_ATTRIBUTES) BYTE bytes[sizeof(SID_AND_ATTRIBUTES) + SECURITY_MAX_SID_SIZE]{};
    DWORD length = 0;
    TokenSid() = default;
    TokenSid(const TokenSid &) = delete;
    TokenSid &operator=(const TokenSid &) = delete;
    PSID sid() const {
        if (length < sizeof(SID_AND_ATTRIBUTES) || length > sizeof(bytes)) return nullptr;
        const auto value = reinterpret_cast<const SID_AND_ATTRIBUTES *>(bytes)->Sid;
        const auto begin = reinterpret_cast<quintptr>(bytes);
        const auto address = reinterpret_cast<quintptr>(value);
        // Check the SID header before asking Win32 to inspect its variable tail.
        if (address < begin + sizeof(SID_AND_ATTRIBUTES)
            || address > begin + length - GetSidLengthRequired(0)) return nullptr;
        const auto count = *GetSidSubAuthorityCount(value);
        if (!count || count > SID_MAX_SUB_AUTHORITIES
            || GetSidLengthRequired(count) > begin + length - address
            || !IsValidSid(value)) return nullptr;
        return value;
    }
    bool read(HANDLE token, TOKEN_INFORMATION_CLASS kind) {
        DWORD written = 0;
        length = 0;
        if (!GetTokenInformation(token, kind, bytes, DWORD(sizeof(bytes)), &written)) return false;
        length = written;
        return sid() != nullptr;
    }
};
bool tokenIntegrityLevel(HANDLE token, DWORD &level) {
    TokenSid information;
    if (!information.read(token, TokenIntegrityLevel)) return false;
    const auto sid = information.sid();
    const SID_IDENTIFIER_AUTHORITY mandatory = SECURITY_MANDATORY_LABEL_AUTHORITY;
    if (*GetSidSubAuthorityCount(sid) != 1
        || std::memcmp(GetSidIdentifierAuthority(sid), &mandatory, sizeof(mandatory)) != 0) return false;
    level = *GetSidSubAuthority(sid, 0);
    return true;
}
struct Security {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    TokenSid user;
    Security() {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return;
        Handle token(raw);
        if (!user.read(raw, TokenUser)) return;
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(user.sid(), &sid)) return;
        // Protected DACL: only this user and the OS, with no inherited broad ACEs.
        const auto sddl = QString("O:%1D:P(A;;FA;;;%1)(A;;FA;;;SY)").arg(QString::fromWCharArray(sid));
        LocalFree(sid);
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr))
            attributes.lpSecurityDescriptor = descriptor;
    }
    ~Security() { if (descriptor) LocalFree(descriptor); }
    PSID sid() const { return user.sid(); }
};
bool privateHandle(HANDLE handle, const Security &security, bool directory) {
    BY_HANDLE_FILE_INFORMATION information{};
    if (!security.sid() || !GetFileInformationByHandle(handle, &information)
        || (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        || bool(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory
        || (!directory && information.nNumberOfLinks != 1)) return false;
    PSID owner = nullptr; PACL acl = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                        &owner, nullptr, &acl, nullptr, &descriptor) != ERROR_SUCCESS) return false;
    bool valid = owner && EqualSid(owner, security.sid()) && acl;
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
    valid = valid && GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED);
    BYTE systemSid[SECURITY_MAX_SID_SIZE]{}; DWORD systemSize = sizeof(systemSid);
    valid = valid && CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSid, &systemSize);
    for (DWORD i = 0; valid && i < acl->AceCount; ++i) {
        void *raw = nullptr;
        if (!GetAce(acl, i, &raw)) { valid = false; break; }
        const auto header = static_cast<ACE_HEADER *>(raw);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) { valid = false; break; }
        const auto ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);
        const auto sid = reinterpret_cast<PSID>(&ace->SidStart);
        valid = EqualSid(sid, security.sid()) || EqualSid(sid, systemSid);
    }
    LocalFree(descriptor);
    return valid;
}
HANDLE openPath(const QString &path, DWORD access, bool directory) {
    return CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), access | READ_CONTROL,
                       FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
}
#else
struct Descriptor {
    int value = -1;
    explicit Descriptor(int descriptor) : value(descriptor) {}
    ~Descriptor() { if (value >= 0) ::close(value); }
};
bool privateDescriptor(int descriptor, bool directory) {
    struct stat information{};
    return descriptor >= 0 && ::fstat(descriptor, &information) == 0
        && information.st_uid == ::geteuid()
        && (information.st_mode & 0777) == (directory ? 0700 : 0600)
        && (directory ? S_ISDIR(information.st_mode) : (S_ISREG(information.st_mode) && information.st_nlink == 1));
}
int openDirectory() {
    const auto path = QFile::encodeName(ipcRuntimeDirectory());
    return ::open(path.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}
#endif
}

QString ipcRuntimeDirectory() {
#ifdef Q_OS_WIN
    return QDir(agentStateLocation()).filePath("ipc-private");
#else
    // Short enough for macOS sockaddr_un, independent of the caller's app name.
    // Qt's per-user TMPDIR also supplies a protected parent on macOS.
    const auto identity = QCryptographicHash::hash(agentStateLocation().toUtf8(), QCryptographicHash::Sha256).toHex().left(12);
    return QDir(QDir::tempPath()).filePath("eh-" + QString::fromLatin1(identity));
#endif
}
QString ipcCredentialPath() { return QDir(ipcRuntimeDirectory()).filePath("token"); }
QByteArray randomIpcToken() {
    quint32 words[8];
    QRandomGenerator::system()->fillRange(words);
    return QByteArray(reinterpret_cast<const char *>(words), sizeof(words)).toHex();
}
QString ipcServerPolicyError() {
#ifdef Q_OS_WIN
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return "Unable to verify the process privilege level. IPC is disabled.";
    Handle token(raw);
    TOKEN_ELEVATION elevation{}; DWORD size = 0;
    if (!GetTokenInformation(raw, TokenElevation, &elevation, sizeof(elevation), &size))
        return "Unable to verify the process privilege level. IPC is disabled.";
    DWORD level = 0;
    if (!tokenIntegrityLevel(raw, level))
        return "Unable to verify the process integrity level. IPC is disabled.";
    if (elevation.TokenIsElevated || level >= SECURITY_MANDATORY_HIGH_RID)
        return "IPC is disabled when EditHere runs with elevated privileges. Start EditHere normally to use the CLI or desktop forwarding.";
#endif
    return {};
}
QString validateIpcDirectory() {
#ifdef Q_OS_WIN
    Security security;
    Handle directory(openPath(ipcRuntimeDirectory(), 0, true));
    return privateHandle(directory.value, security, true) ? QString() : unsafePath;
#else
    Descriptor directory(openDirectory());
    return privateDescriptor(directory.value, true) ? QString() : unsafePath;
#endif
}
QString createIpcCredentials(QByteArray &token) {
    token.clear();
    if (const auto error = ipcServerPolicyError(); !error.isEmpty()) return error;
    const auto fresh = randomIpcToken();
#ifdef Q_OS_WIN
    Security security;
    if (!security.descriptor) return unsafePath;
    if (!QDir().mkpath(agentStateLocation())) return unsafePath;
    // The application state parent is created by the single-instance owner.
    if (!CreateDirectoryW(reinterpret_cast<LPCWSTR>(ipcRuntimeDirectory().utf16()), &security.attributes)
        && GetLastError() != ERROR_ALREADY_EXISTS) return unsafePath;
    Handle directory(openPath(ipcRuntimeDirectory(), 0, true));
    if (!privateHandle(directory.value, security, true)) return unsafePath;
    const auto target = ipcCredentialPath();
    {
        Handle existing(openPath(target, 0, false));
        if (existing.value != INVALID_HANDLE_VALUE && !privateHandle(existing.value, security, false)) return unsafePath;
        if (existing.value == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) return unsafePath;
    }
    const auto temporary = QDir(ipcRuntimeDirectory()).filePath("token-" + QString::fromLatin1(randomIpcToken().left(16)));
    bool written = false;
    bool created = false;
    {
        Handle file(CreateFileW(reinterpret_cast<LPCWSTR>(temporary.utf16()), GENERIC_WRITE | READ_CONTROL, 0,
                                &security.attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD count = 0;
        created = file.value != INVALID_HANDLE_VALUE;
        written = file.value != INVALID_HANDLE_VALUE && privateHandle(file.value, security, false)
            && WriteFile(file.value, fresh.constData(), DWORD(fresh.size()), &count, nullptr)
            && count == DWORD(fresh.size()) && FlushFileBuffers(file.value);
    }
    if (!written || !MoveFileExW(reinterpret_cast<LPCWSTR>(temporary.utf16()), reinterpret_cast<LPCWSTR>(target.utf16()),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (created) DeleteFileW(reinterpret_cast<LPCWSTR>(temporary.utf16()));
        return unsafePath;
    }
#else
    const auto path = QFile::encodeName(ipcRuntimeDirectory());
    if (::mkdir(path.constData(), 0700) != 0 && errno != EEXIST) return unsafePath;
    Descriptor directory(openDirectory());
    if (!privateDescriptor(directory.value, true)) return unsafePath;
    struct stat existing{};
    if (::fstatat(directory.value, "token", &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(existing.st_mode) || existing.st_uid != ::geteuid() || (existing.st_mode & 0777) != 0600 || existing.st_nlink != 1)
            return unsafePath;
    } else if (errno != ENOENT) return unsafePath;
    const auto temporary = "token-" + randomIpcToken().left(16);
    Descriptor file(::openat(directory.value, temporary.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (file.value < 0) return unsafePath;
    if (!privateDescriptor(file.value, false)) {
        ::unlinkat(directory.value, temporary.constData(), 0); return unsafePath;
    }
    qsizetype offset = 0;
    while (offset < fresh.size()) {
        const auto count = ::write(file.value, fresh.constData() + offset, size_t(fresh.size() - offset));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += count;
    }
    const bool written = offset == fresh.size() && ::fsync(file.value) == 0;
    if (!written || ::renameat(directory.value, temporary.constData(), directory.value, "token") != 0) {
        ::unlinkat(directory.value, temporary.constData(), 0); return unsafePath;
    }
#endif
    token = fresh;
    return {};
}
QString readIpcCredentials(QByteArray &token) {
    token.clear();
    if (const auto error = validateIpcDirectory(); !error.isEmpty()) return error;
    QByteArray bytes(65, '\0');
#ifdef Q_OS_WIN
    Security security;
    Handle directory(openPath(ipcRuntimeDirectory(), 0, true));
    if (!privateHandle(directory.value, security, true)) return unsafePath;
    Handle file(openPath(ipcCredentialPath(), GENERIC_READ, false));
    if (!privateHandle(file.value, security, false)) return unsafePath;
    DWORD count = 0;
    if (!ReadFile(file.value, bytes.data(), DWORD(bytes.size()), &count, nullptr)) return unsafePath;
    bytes.resize(int(count));
#else
    Descriptor directory(openDirectory());
    if (!privateDescriptor(directory.value, true)) return unsafePath;
    Descriptor file(::openat(directory.value, "token", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
    if (!privateDescriptor(file.value, false)) return unsafePath;
    ssize_t count = -1;
    do { count = ::read(file.value, bytes.data(), size_t(bytes.size())); } while (count < 0 && errno == EINTR);
    if (count < 0) return unsafePath;
    bytes.resize(qsizetype(count));
#endif
    if (bytes.size() != 64 || bytes.toLower() != bytes || QByteArray::fromHex(bytes).toHex() != bytes)
        return "EditHere's IPC credential is missing or invalid. Restart the desktop app normally.";
    token = bytes;
    return {};
}
void removeIpcCredentials(const QByteArray &token) {
    QByteArray current;
    if (!token.isEmpty() && readIpcCredentials(current).isEmpty() && current == token)
        QFile::remove(ipcCredentialPath());
}
QString ipcPeerError(const QLocalSocket &socket, bool serverSide) {
#ifdef Q_OS_WIN
    if (const auto error = ipcServerPolicyError(); !error.isEmpty()) return error;
    const auto pipe = reinterpret_cast<HANDLE>(socket.socketDescriptor());
    ULONG processId = 0;
    const bool identified = serverSide ? GetNamedPipeClientProcessId(pipe, &processId) : GetNamedPipeServerProcessId(pipe, &processId);
    if (!identified) return "Unable to verify the IPC peer identity.";
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    HANDLE raw = nullptr;
    if (process.value == INVALID_HANDLE_VALUE || !OpenProcessToken(process.value, TOKEN_QUERY, &raw))
        return "Unable to verify the IPC peer identity.";
    Handle peerToken(raw);
    TokenSid user;
    if (!user.read(raw, TokenUser)) return "Unable to verify the IPC peer identity.";
    Security local;
    if (!local.sid() || !EqualSid(local.sid(), user.sid()))
        return "IPC requests from another OS user are not permitted.";
    DWORD peerLevel = 0;
    if (!tokenIntegrityLevel(raw, peerLevel)) return "Unable to verify the IPC peer integrity level.";
    HANDLE localRaw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &localRaw)) return "Unable to verify the IPC integrity level.";
    Handle localToken(localRaw);
    DWORD localLevel = 0;
    if (!tokenIntegrityLevel(localRaw, localLevel)) return "Unable to verify the IPC integrity level.";
    if (peerLevel != localLevel)
        return "IPC between different process integrity levels is not permitted.";
    return {};
#else
    Q_UNUSED(serverSide);
    const auto descriptor = socket.socketDescriptor();
    if (descriptor < 0) return "Unable to verify the IPC peer identity.";
    uid_t user = uid_t(-1);
#ifdef Q_OS_MACOS
    gid_t group = gid_t(-1);
    if (::getpeereid(int(descriptor), &user, &group) != 0) return "Unable to verify the IPC peer identity.";
#else
    struct { pid_t process; uid_t user; gid_t group; } credentials{};
    socklen_t length = sizeof(credentials);
    if (::getsockopt(int(descriptor), SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0 || length != sizeof(credentials))
        return "Unable to verify the IPC peer identity.";
    user = credentials.user;
#endif
    return user == ::geteuid() ? QString() : QString("IPC requests from another OS user are not permitted.");
#endif
}
bool ipcTokenMatches(const QByteArray &expected, const QString &received) {
    const auto bytes = received.toLatin1();
    if (expected.size() != 64 || bytes.size() != expected.size()) return false;
    unsigned char difference = 0;
    for (qsizetype i = 0; i < expected.size(); ++i)
        difference |= static_cast<unsigned char>(expected[i]) ^ static_cast<unsigned char>(bytes[i]);
    return difference == 0;
}
} // namespace h2d
