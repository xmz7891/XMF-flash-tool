#include <QCoreApplication>
#include <QTest>

#include "TestRegistry.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("XMF"));
    QCoreApplication::setApplicationName(QStringLiteral("XMF Flash Tool Tests"));

    int failed = 0;
    int executed = 0;
    for (const auto& entry : xmf_test::registry()) {
        QObject* test = entry.factory();
        if (!test)
            continue;
        ++executed;
        int result = 1;
        try {
            result = QTest::qExec(test, argc, argv);
        } catch (const std::exception& e) {
            fprintf(stderr, "FAILED (uncaught exception): %s -> %s\n", qPrintable(entry.name), e.what());
            result = 1;
        } catch (...) {
            fprintf(stderr, "FAILED (uncaught exception): %s\n", qPrintable(entry.name));
            result = 1;
        }
        if (result != 0) {
            ++failed;
            fprintf(stderr, "FAILED: %s\n", qPrintable(entry.name));
        } else {
            fprintf(stdout, "PASSED: %s\n", qPrintable(entry.name));
        }
        delete test;
    }
    fprintf(stdout, "\n%d test class(es), %d failed\n", executed, failed);
    return failed == 0 ? 0 : 1;
}
