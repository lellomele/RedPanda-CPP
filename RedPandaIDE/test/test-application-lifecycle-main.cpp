// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QTest>
#include "test_application_lifecycle.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TestApplicationLifecycle suite;
    return QTest::qExec(&suite, argc, argv);
}
