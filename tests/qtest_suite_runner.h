#ifndef QTEST_SUITE_RUNNER_H
#define QTEST_SUITE_RUNNER_H
#include <QTest>
#include <QTextStream>
#include <QVector>
#include <functional>
#include <initializer_list>

namespace TestSuites {
struct Suite { const char *name; std::function<int(int, char **)> run; };
template<class T> Suite suite(const char *name) {
    return {name, [](int argc, char **argv) { T test; return QTest::qExec(&test, argc, argv); }};
}
inline int run(int argc, char **argv, std::initializer_list<Suite> suites) {
    QString selected;
    bool seenSuite = false, listSuites = false;
    QVector<char *> filtered;
    for (int i = 0; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--list-suites") {
            if (listSuites) return 2;
            listSuites = true;
            continue;
        }
        if (QString::fromLocal8Bit(argv[i]) == "--suite") {
            if (seenSuite || i + 1 >= argc) return 2;
            seenSuite = true;
            selected = QString::fromLocal8Bit(argv[++i]);
            if (selected.isEmpty()) return 2;
        } else filtered.append(argv[i]);
    }
    if (listSuites) {
        if (seenSuite || filtered.size() != 1) return 2;
        QTextStream output(stdout);
        for (const auto &s : suites) output << s.name << '\n';
        return 0;
    }
    const int count = filtered.size();
    filtered.append(nullptr);
    int status = 0;
    bool found = selected.isEmpty();
    for (const auto &s : suites) {
        if (!selected.isEmpty() && selected != QLatin1String(s.name)) continue;
        found = true;
        status |= s.run(count, filtered.data());
    }
    if (!found) { QTextStream(stderr) << "Unknown test suite: " << selected << '\n'; return 2; }
    return status;
}
}
#endif
