#include "ToolRegistry.h"

#include <QCollator>
#include <QDebug>

namespace gf {

ToolRegistry& ToolRegistry::instance()
{
    static ToolRegistry registry;
    return registry;
}

void ToolRegistry::add(const ToolSpec& spec, ToolHandler handler)
{
    if (spec.id.isEmpty()) {
        qWarning() << "ToolRegistry: tool with empty id ignored";
        return;
    }
    if (m_index.contains(spec.id)) {
        qWarning() << "ToolRegistry: duplicate tool id" << spec.id;
        return;
    }
    m_index.insert(spec.id, int(m_specs.size()));
    m_specs.push_back(spec);
    if (handler)
        m_handlers.insert(spec.id, std::move(handler));
}

void ToolRegistry::addPlanned(const ToolSpec& spec)
{
    ToolSpec s = spec;
    s.maturity = 2;
    add(s, nullptr);
}

QVariantList ToolRegistry::tools() const
{
    QVariantList out;
    out.reserve(int(m_specs.size()));
    for (const auto& s : m_specs) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), s.id);
        m.insert(QStringLiteral("category"), s.category);
        m.insert(QStringLiteral("group"), s.group);
        m.insert(QStringLiteral("name"), s.name);
        m.insert(QStringLiteral("desc"), s.desc);
        m.insert(QStringLiteral("icon"), s.icon);
        m.insert(QStringLiteral("keywords"), s.keywords.join(QLatin1Char(' ')));
        m.insert(QStringLiteral("fields"), s.fields);
        m.insert(QStringLiteral("maturity"), s.maturity);
        m.insert(QStringLiteral("badge"), s.badge);
        m.insert(QStringLiteral("order"), s.order);
        m.insert(QStringLiteral("runnable"), m_handlers.contains(s.id));
        out.append(m);
    }
    return out;
}

QVariantList ToolRegistry::categories() const
{
    // The 19 functional areas of the product specification.
    struct Cat { const char* id; const char* name; const char* icon; const char* color; };
    static const Cat cats[] = {
        {"image", "固件 & 镜像", "disc-3", "#5B8CFF"},
        {"qcom", "高通 9008", "cpu", "#E05A5A"},
        {"mtk", "联发科底层", "microchip", "#4EA6F2"},
        {"bootloader", "Bootloader", "shield-check", "#8B7CF6"},
        {"sprd", "紫光展锐", "layers", "#F2A93B"},
        {"rk", "瑞芯微 RK", "box", "#57C7A3"},
        {"aw", "全志 A 系列", "sun", "#B98CFF"},
        {"samsung", "三星 Samsung", "tablet", "#4DD0E1"},
        {"root", "Root / KSU / 面具", "key-round", "#34D399"},
        {"unlock", "账号锁 / 清除", "lock-keyhole", "#FB7185"},
        {"adb", "ADB / Fastboot", "terminal", "#38BDF8"},
        {"firmware", "固件资源", "download-cloud", "#A3E635"},
        {"batch", "批量 / 自动化", "list-checks", "#FBBF24"},
        {"bypass", "机型旁路 / 工程", "smartphone", "#F472B6"},
    };
    QVariantList out;
    for (const auto& c : cats) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QString::fromLatin1(c.id));
        m.insert(QStringLiteral("name"), QString::fromUtf8(c.name));
        m.insert(QStringLiteral("icon"), QString::fromLatin1(c.icon));
        m.insert(QStringLiteral("color"), QString::fromLatin1(c.color));
        out.append(m);
    }
    return out;
}

QVariantMap ToolRegistry::tool(const QString& id) const
{
    auto it = m_index.constFind(id);
    if (it == m_index.constEnd())
        return {};
    const auto& s = m_specs[size_t(it.value())];
    QVariantMap m;
    m.insert(QStringLiteral("id"), s.id);
    m.insert(QStringLiteral("category"), s.category);
    m.insert(QStringLiteral("name"), s.name);
    m.insert(QStringLiteral("desc"), s.desc);
    m.insert(QStringLiteral("icon"), s.icon);
    m.insert(QStringLiteral("group"), s.group);
    m.insert(QStringLiteral("keywords"), s.keywords.join(QLatin1Char(' ')));
    m.insert(QStringLiteral("fields"), s.fields);
    m.insert(QStringLiteral("maturity"), s.maturity);
    m.insert(QStringLiteral("badge"), s.badge);
    m.insert(QStringLiteral("runnable"), m_handlers.contains(s.id));
    return m;
}

QVariantList ToolRegistry::fieldsOf(const QString& id) const
{
    return tool(id).value(QStringLiteral("fields")).toList();
}

bool ToolRegistry::has(const QString& id) const
{
    return m_index.contains(id);
}

ToolHandler ToolRegistry::handler(const QString& id) const
{
    return m_handlers.value(id);
}

namespace fields {

static QVariantMap base(const QString& key, const QString& label, const QString& type,
                        const QVariant& def, const QString& hint, bool required)
{
    QVariantMap f;
    f.insert(QStringLiteral("key"), key);
    f.insert(QStringLiteral("label"), label);
    f.insert(QStringLiteral("type"), type);
    f.insert(QStringLiteral("default"), def);
    f.insert(QStringLiteral("hint"), hint);
    f.insert(QStringLiteral("required"), required);
    return f;
}

QVariantMap text(const QString& key, const QString& label, const QString& def, const QString& placeholder,
                 const QString& hint, bool required)
{
    auto f = base(key, label, QStringLiteral("text"), def, hint, required);
    f.insert(QStringLiteral("placeholder"), placeholder);
    return f;
}

QVariantMap path(const QString& key, const QString& label, const QString& filter, const QString& hint,
                 bool required)
{
    auto f = base(key, label, QStringLiteral("path"), QString(), hint, required);
    f.insert(QStringLiteral("filter"), filter);
    return f;
}

QVariantMap paths(const QString& key, const QString& label, const QString& filter, const QString& hint)
{
    auto f = base(key, label, QStringLiteral("paths"), QVariantList(), hint, false);
    f.insert(QStringLiteral("filter"), filter);
    return f;
}

QVariantMap dir(const QString& key, const QString& label, const QString& hint, bool required)
{
    return base(key, label, QStringLiteral("dir"), QString(), hint, required);
}

QVariantMap boolean(const QString& key, const QString& label, bool def, const QString& hint)
{
    return base(key, label, QStringLiteral("bool"), def, hint, false);
}

QVariantMap integer(const QString& key, const QString& label, int def, int minimum, int maximum,
                    const QString& hint)
{
    auto f = base(key, label, QStringLiteral("int"), def, hint, false);
    f.insert(QStringLiteral("min"), minimum);
    f.insert(QStringLiteral("max"), maximum);
    return f;
}

QVariantMap hex(const QString& key, const QString& label, const QString& def, const QString& hint)
{
    return base(key, label, QStringLiteral("hex"), def, hint, false);
}

QVariantMap choice(const QString& key, const QString& label, const QStringList& options, int def,
                   const QString& hint, bool editable)
{
    auto f = base(key, label, QStringLiteral("choice"), def, hint, false);
    f.insert(QStringLiteral("options"), options);
    f.insert(QStringLiteral("editable"), editable);
    return f;
}

} // namespace fields

void addTool(const ToolSpec& spec, ToolHandler handler)
{
    ToolRegistry::instance().add(spec, std::move(handler));
}

} // namespace gf
