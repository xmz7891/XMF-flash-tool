#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <vector>
#include "TaskManager.h"

namespace gf {

/// Description of one user-facing tool. The whole UI is generated from these
/// specs: a card in the tool grid, a fly-out form sheet built from `fields`,
/// and a background handler registered with the same id.
struct ToolSpec {
    QString id;         // stable unique id, e.g. "image.payload.unpack"
    QString category;   // category id, see ToolRegistry::categories()
    QString group;      // optional sub group inside a category
    QString name;       // display name
    QString desc;       // one line description
    QString icon;       // lucide icon name
    QStringList keywords;
    QVariantList fields;  // see fields:: helpers
    int maturity = 0;     // 0 = ready, 1 = beta, 2 = planned
    QString badge;        // optional extra badge text
    int order = 1000;
};

using ToolHandler = std::function<void(TaskContext&, const QVariantMap&)>;

class ToolRegistry {
public:
    static ToolRegistry& instance();

    void add(const ToolSpec& spec, ToolHandler handler);
    /// Registers a tool that exists in the UI but whose backend is not ready yet.
    void addPlanned(const ToolSpec& spec);

    QVariantList tools() const;
    QVariantList categories() const;
    QVariantMap tool(const QString& id) const;
    QVariantList fieldsOf(const QString& id) const;
    bool has(const QString& id) const;
    ToolHandler handler(const QString& id) const;
    int count() const { return int(m_specs.size()); }

private:
    ToolRegistry() = default;
    std::vector<ToolSpec> m_specs;
    QHash<QString, int> m_index;
    QHash<QString, ToolHandler> m_handlers;
};

namespace fields {

/// Helpers that build a form-field description for ToolSpec::fields.
QVariantMap text(const QString& key, const QString& label, const QString& def = {},
                 const QString& placeholder = {}, const QString& hint = {}, bool required = true);
QVariantMap path(const QString& key, const QString& label, const QString& filter = {},
                 const QString& hint = {}, bool required = true);
QVariantMap paths(const QString& key, const QString& label, const QString& filter = {},
                  const QString& hint = {});
QVariantMap dir(const QString& key, const QString& label, const QString& hint = {}, bool required = true);
QVariantMap boolean(const QString& key, const QString& label, bool def = false, const QString& hint = {});
QVariantMap integer(const QString& key, const QString& label, int def = 0, int minimum = 0,
                    int maximum = 2147483647, const QString& hint = {});
QVariantMap hex(const QString& key, const QString& label, const QString& def = {},
                const QString& hint = {});
QVariantMap choice(const QString& key, const QString& label, const QStringList& options, int def = 0,
                   const QString& hint = {}, bool editable = false);

} // namespace fields

/// Shortcut used by every module: register a ready tool.
void addTool(const ToolSpec& spec, ToolHandler handler);

} // namespace gf
