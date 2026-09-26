// Keeps a secret (the Wi-Fi password the camera joins with) in the app's settings without storing it
// in the clear: on Windows it is encrypted with DPAPI, for the current Windows user only.
#pragma once

#include <QByteArray>
#include <QString>

namespace secret {

// The value to store (base64); empty if encryption failed.
QByteArray protect(const QString& secret);
// The secret back; empty if the value is not ours (another user, damaged).
QString unprotect(const QByteArray& stored);

}  // namespace secret
