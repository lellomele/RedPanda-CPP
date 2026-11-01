// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
#ifndef GDBMIARGUMENTS_H
#define GDBMIARGUMENTS_H

#include <QByteArray>

namespace GDBMI {

// Encode one MI C-string after choosing the debugger's character encoding.
// Fixed-width octal escapes cannot absorb a following digit from the expression.
inline QByteArray quoteArgument(const QByteArray &argument)
{
    QByteArray result;
    result.reserve(argument.size() + 2);
    result += '"';
    for (unsigned char ch : argument) {
        if (ch == '"' || ch == '\\') {
            result += '\\';
            result += static_cast<char>(ch);
        } else if (ch < 32 || ch == 127) {
            result += '\\';
            result += static_cast<char>('0' + ((ch >> 6) & 7));
            result += static_cast<char>('0' + ((ch >> 3) & 7));
            result += static_cast<char>('0' + (ch & 7));
        } else {
            result += static_cast<char>(ch);
        }
    }
    result += '"';
    return result;
}

inline QByteArray watchCreationArguments(const QByteArray &expression, bool lldb)
{
    return (lldb ? QByteArray(" - * ") : QByteArray(" - @ ")) + quoteArgument(expression);
}

}
#endif
