#pragma once
#include <QString>
namespace gf::qcom {
/// Runs the bundled open-source qedl backend once to upload the Sahara loader
/// and leave the device in Firehose. Safe to call when the device is already
/// in Firehose: qedl just reads the GPT and exits.
bool bootstrapLoader(const QString& programmerPath, const QString& port, QString* error = nullptr);
QString qedlExecutable();
}
