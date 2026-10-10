#ifndef TEST_KEYSTROKES_H
#define TEST_KEYSTROKES_H

#include <QObject>

namespace QSynedit {
class TestKeyStrokes : public QObject
{
    Q_OBJECT
private slots:
    void test_key_sequence_data();
    void test_key_sequence();
    void test_single_key_clears_second_key();
};
}
#endif
