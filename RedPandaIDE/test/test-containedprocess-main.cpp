// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QTest>
#include "test_containedprocess.h"
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const int helper = runContainedProcessTestHelper();
    if (helper >= 0) return helper;
    TestContainedProcess tests;
    return QTest::qExec(&tests, argc, argv);
}
