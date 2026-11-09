#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <vector>

namespace xmf_test {

struct Entry {
    QString name;
    std::function<QObject*()> factory;
};

inline std::vector<Entry>& registry()
{
    static std::vector<Entry> entries;
    return entries;
}

struct Registrar {
    Registrar(const char* name, std::function<QObject*()> factory)
    {
        registry().push_back({QString::fromUtf8(name), std::move(factory)});
    }
};

} // namespace xmf_test

/// Registers a QTest class with the shared test runner. The class must contain
/// Q_OBJECT and the translation unit must end with `#include "this_file.moc"`.
/// Usage:
///     class TestFoo : public QObject { Q_OBJECT private slots: void bar(); };
///     GF_TEST_CLASS(TestFoo)
#define GF_TEST_CLASS(Class)                                                                  \
    static ::xmf_test::Registrar gf_test_registrar_##Class(#Class, []() -> QObject* {          \
        return new Class();                                                                   \
    });
