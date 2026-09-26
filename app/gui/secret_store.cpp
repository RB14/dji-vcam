#include "secret_store.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

namespace secret {

#ifdef Q_OS_WIN
namespace {

QByteArray run(const QByteArray& input, bool encrypt) {
    DATA_BLOB in{static_cast<DWORD>(input.size()), reinterpret_cast<BYTE*>(const_cast<char*>(input.constData()))};
    DATA_BLOB out{};
    const BOOL ok = encrypt ? CryptProtectData(&in, L"dji-vcam", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                            : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) {
        return {};
    }
    QByteArray result(reinterpret_cast<const char*>(out.pbData), static_cast<qsizetype>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

}  // namespace

QByteArray protect(const QString& secret) {
    const QByteArray encrypted = run(secret.toUtf8(), true);
    return encrypted.isEmpty() ? QByteArray() : encrypted.toBase64();
}

QString unprotect(const QByteArray& stored) {
    return QString::fromUtf8(run(QByteArray::fromBase64(stored), false));
}
#else
// No system key store wired up yet outside Windows: obfuscated only (Linux support is in progress).
QByteArray protect(const QString& secret) { return secret.toUtf8().toBase64(); }

QString unprotect(const QByteArray& stored) { return QString::fromUtf8(QByteArray::fromBase64(stored)); }
#endif

}  // namespace secret
