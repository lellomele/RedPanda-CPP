#ifndef TEST_ATOMIC_REPLACE_LOCK_H
#define TEST_ATOMIC_REPLACE_LOCK_H
#include <QString>
#include <QtGlobal>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Model another program holding a file without delete-sharing. Reads and
// writes remain allowed, but an atomic replacement must fail at commit.
class AtomicReplaceLock {
public:
    explicit AtomicReplaceLock(const QString &path):
        mHandle(CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
    ~AtomicReplaceLock() { release(); }
    bool locked() const { return mHandle != INVALID_HANDLE_VALUE; }
    void release() {
        if (locked()) { CloseHandle(mHandle); mHandle = INVALID_HANDLE_VALUE; }
    }
    AtomicReplaceLock(const AtomicReplaceLock &) = delete;
    AtomicReplaceLock &operator=(const AtomicReplaceLock &) = delete;
private:
    HANDLE mHandle = INVALID_HANDLE_VALUE;
};
#endif
#endif
