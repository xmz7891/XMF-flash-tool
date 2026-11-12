#pragma once

#include <QString>

namespace gf::notice {
/// Verifies the encrypted free-notice payload and unlocks the application.
bool verify();
/// Decrypted notice text. Empty when verify() fails.
QString text();
}
