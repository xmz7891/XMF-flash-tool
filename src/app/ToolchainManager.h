#pragma once
#include <QString>
namespace gf {
/// Extracts the bundled vendor-tool archive on first run and verifies its
/// SHA256 manifest. Returns false (and a reason) when the toolchain is corrupt.
bool ensureToolchain(QString* error = nullptr);
}
