// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_semanticretirement.h"
#include <QApplication>
#include <QTest>
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    TestSemanticRetirement tests;
    return QTest::qExec(&tests, argc, argv);
}
