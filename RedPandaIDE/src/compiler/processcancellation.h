// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PROCESSCANCELLATION_H
#define PROCESSCANCELLATION_H

#include <QProcess>

namespace ProcessCancellation {
constexpr int pollMilliseconds = 100;
constexpr int terminateMilliseconds = 500;
constexpr int killMilliseconds = 2000;

// Call only from the worker that owns this QProcess. The GUI requests stop
// through an atomic flag and never waits for a child process or a semaphore.
inline bool stop(QProcess &process)
{
    if (process.state() == QProcess::NotRunning)
        return true;
    process.terminate();
    if (process.waitForFinished(terminateMilliseconds))
        return true;
    process.kill();
    return process.waitForFinished(killMilliseconds)
            || process.state() == QProcess::NotRunning;
}
}
#endif
