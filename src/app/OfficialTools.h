#pragma once

#include <QObject>
#include <QVariantList>

namespace gf {

/// Locates the bundled official vendor flashing tools (MiFlash, SP Flash Tool,
/// Unisoc Research Download) and the driver packages that ship with them.
/// The tools keep running as their own processes; the in-app workbenches are
/// still the primary UX and use this folder for loaders / DA files / drivers.
class OfficialTools : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tools READ tools NOTIFY toolsChanged)
    Q_PROPERTY(QVariantList drivers READ drivers NOTIFY toolsChanged)
    Q_PROPERTY(QString root READ root CONSTANT)

public:
    explicit OfficialTools(QObject* parent = nullptr);

    QVariantList tools() const;
    QVariantList drivers() const;
    QString root() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool launch(const QString& id);
    Q_INVOKABLE void openFolder(const QString& id);
    Q_INVOKABLE void openRoot();
    Q_INVOKABLE QString toolPath(const QString& id) const;

signals:
    void toolsChanged();

private:
    struct ToolDef {
        QString id;
        QString name;
        QString description;
        QString icon;
        QString relativeExe;
        QString relativeDir;
        QString kind;  // "gui" | "cli"
    };
    QVector<ToolDef> definitions() const;
    QVariantList m_tools;
    QVariantList m_drivers;
};

} // namespace gf
