#pragma once
#include <QString>
#include <QtGlobal>
namespace gf::usb {
/// Best-effort current USB link speed label for a VID/PID pair.
QString linkSpeedText(quint16 vid, quint16 pid);
}
