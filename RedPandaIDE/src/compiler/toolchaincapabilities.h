// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TOOLCHAINCAPABILITIES_H
#define TOOLCHAINCAPABILITIES_H
#include "boundedprocessprobe.h"
#include <QFile>
#include <QFileInfo>
#include <QOperatingSystemVersion>
#include <QXmlStreamReader>
#include <cstring>
#include <limits>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Compilation {
struct CapabilityProbeResult {
    PreparationResult preparation;
    CapabilityFacts facts;
    ProcessOwnershipLevel ownership = ProcessOwnershipLevel::NoProcessStarted;
};

// Bounded PE import inspection preserves the existing output-codec heuristic
// without following unchecked RVAs/pointers in an arbitrary configured binary.
inline bool peImportsUnicodeArguments(const QByteArray &bytes)
{
    const auto contains = [&](quint64 offset, quint64 size) {
        return offset <= quint64(bytes.size()) && size <= quint64(bytes.size()) - offset;
    };
    const auto integer = [&](quint64 offset, int size) -> quint64 {
        if (!contains(offset, size)) return 0;
        quint64 result = 0;
        for (int i = 0; i < size; ++i) result |= quint64(uchar(bytes.at(offset + i))) << (i * 8);
        return result;
    };
    if (!contains(0, 64) || bytes.left(2) != "MZ") return false;
    const quint64 pe = integer(0x3c, 4);
    if (!contains(pe, 24) || bytes.mid(pe, 4) != QByteArray("PE\0\0", 4)) return false;
    const quint64 sections = integer(pe + 6, 2), optionalSize = integer(pe + 20, 2);
    const quint64 optional = pe + 24, magic = integer(optional, 2);
    if (sections > 512 || !contains(optional, optionalSize) || (magic != 0x10b && magic != 0x20b)) return false;
    const quint64 directories = optional + (magic == 0x20b ? 112 : 96);
    if (directories + 16 > optional + optionalSize) return false;
    const quint64 sectionTable = optional + optionalSize;
    if (!contains(sectionTable, sections * 40)) return false;
    quint64 work = 0;
    constexpr quint64 workLimit = 100000;
    const auto rawOffset = [&](quint64 rva, quint64 needed) -> quint64 {
        for (quint64 i = 0; i < sections; ++i) {
            if (++work > workLimit) break;
            const quint64 section = sectionTable + i * 40;
            const quint64 virtualAddress = integer(section + 12, 4);
            const quint64 rawSize = integer(section + 16, 4), raw = integer(section + 20, 4);
            if (rva >= virtualAddress && rva - virtualAddress <= rawSize
                    && needed <= rawSize - (rva - virtualAddress) && contains(raw + rva - virtualAddress, needed))
                return raw + rva - virtualAddress;
        }
        return quint64(bytes.size());
    };
    const auto nameAt = [&](quint64 rva) {
        const quint64 offset = rawOffset(rva, 1);
        if (!contains(offset, 1)) return QByteArray{};
        const auto available = qMin<quint64>(513, quint64(bytes.size()) - offset);
        const auto *begin = bytes.constData() + offset;
        const auto *end = static_cast<const char *>(std::memchr(begin, 0, available));
        if (!end) return QByteArray{};
        return QByteArray(begin, end - begin);
    };
    const quint64 importRva = integer(directories + 8, 4), importSize = integer(directories + 12, 4);
    if (!importRva || importSize < 20) return false;
    for (quint64 i = 0; i < qMin<quint64>(4096, importSize / 20); ++i) {
        if (work >= workLimit) return false;
        const quint64 descriptor = rawOffset(importRva + i * 20, 20);
        if (!contains(descriptor, 20)) return false;
        const quint64 thunkRva = integer(descriptor, 4), nameRva = integer(descriptor + 12, 4);
        if (!thunkRva && !nameRva) break;
        const auto dll = nameAt(nameRva).toLower();
        if (dll == "cygwin1.dll" || dll == "msys-2.0.dll") return true;
        const QByteArray wanted = dll == "kernel32.dll" ? QByteArray("GetCommandLineW")
            : dll == "msvcrt.dll" ? QByteArray("__wgetmainargs")
            : dll == "api-ms-win-crt-runtime-l1-1-0.dll" ? QByteArray("_configure_wide_argv") : QByteArray{};
        if (wanted.isEmpty() || !thunkRva) continue;
        const quint64 width = magic == 0x20b ? 8 : 4;
        for (quint64 n = 0; n < 65536; ++n) {
            if (work >= workLimit) return false;
            const quint64 thunk = rawOffset(thunkRva + n * width, width);
            if (!contains(thunk, width)) break;
            const quint64 value = integer(thunk, width);
            if (!value) break;
            if (value & (quint64(1) << (width * 8 - 1))) continue;
            if (value > std::numeric_limits<quint32>::max() - 2) continue;
            if (nameAt(value + 2) == wanted) return true;
        }
    }
    return false;
}

inline bool executableHasUtf8Manifest(const QString &path)
{
#ifdef Q_OS_WIN
    if (QOperatingSystemVersion::current().microVersion() < 18362) return false;
    HMODULE module = LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!module) return false;
    struct Release { HMODULE module; ~Release() { FreeLibrary(module); } } release{module};
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
    if (!resource) return false;
    const DWORD size = SizeofResource(module, resource);
    if (size > 1024 * 1024) return false;
    const auto loaded = LoadResource(module, resource);
    if (!loaded) return false;
    const auto *data = static_cast<const char *>(LockResource(loaded));
    if (!data) return false;
    QXmlStreamReader xml(QByteArrayView(data, size));
    for (int tokens = 0; !xml.atEnd() && tokens < 8192; ++tokens) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::DTD) return false;
        if (xml.isStartElement() && xml.name() == QStringLiteral("activeCodePage"))
            return xml.readElementText() == QStringLiteral("UTF-8");
    }
#else
    Q_UNUSED(path)
#endif
    return false;
}

struct CompilerBinaryRead {
    PreparationResult preparation;
    QByteArray bytes;
};

inline CompilerBinaryRead readBoundedCompilerBinary(const QString &path, const std::atomic_bool &cancelled)
{
    const auto failure = [](const QString &reason) { return CompilerBinaryRead{PreparationResult::failure(reason), {}}; };
    if (cancelled.load(std::memory_order_acquire)) return {{PreparationStatus::Cancelled, {}}, {}};
    const QFileInfo before(path);
    constexpr qint64 limit = 64LL * 1024 * 1024;
    const qint64 expected = before.size();
    if (!before.isFile() || expected < 0 || expected > limit)
        return failure(QStringLiteral("Could not inspect the compiler binary within the size limit."));
    QFile binary(path);
    if (!binary.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
        return failure(QStringLiteral("Could not open the compiler binary for inspection."));
    CompilerBinaryRead result;
    result.bytes.reserve(expected);
    QElapsedTimer elapsed; elapsed.start();
    while (!binary.atEnd()) {
        if (cancelled.load(std::memory_order_acquire)) return {{PreparationStatus::Cancelled, {}}, {}};
        if (elapsed.elapsed() >= 5000)
            return failure(QStringLiteral("Compiler binary inspection exceeded the read deadline."));
        const qint64 remaining = expected - result.bytes.size();
        if (remaining <= 0)
            return failure(QStringLiteral("The compiler binary grew while being inspected."));
        const auto chunk = binary.read(qMin<qint64>(64 * 1024, remaining));
        if (binary.error() != QFile::NoError || chunk.isEmpty())
            return failure(QStringLiteral("Could not read the compiler binary for inspection."));
        result.bytes.append(chunk);
    }
    if (cancelled.load(std::memory_order_acquire)) return {{PreparationStatus::Cancelled, {}}, {}};
    if (elapsed.elapsed() >= 5000)
        return failure(QStringLiteral("Compiler binary inspection exceeded the read deadline."));
    const QFileInfo after(path);
    if (result.bytes.size() != expected || after.size() != expected || after.lastModified() != before.lastModified())
        return failure(QStringLiteral("The compiler binary changed while being inspected."));
    return result;
}

inline CapabilityProbeResult probeToolchainCapabilities(const ConfigurationValues &configuration,
                                                        const std::atomic_bool &cancelled)
{
    CapabilityProbeResult result;
    const auto *application = QCoreApplication::instance();
    if (application && QThread::currentThread() == application->thread()) {
        result.preparation = PreparationResult::failure(QStringLiteral("Toolchain capabilities must be prepared on a worker thread."));
        return result;
    }
    if (cancelled.load(std::memory_order_acquire)) {
        result.preparation = {PreparationStatus::Cancelled, {}};
        return result;
    }
    const auto &toolchain = configuration.toolchain;
    const QString executable = toolchain.cCompiler.isEmpty() ? toolchain.cppCompiler : toolchain.cCompiler;
#ifdef Q_OS_WIN
    if (configuration.preferences.systemEncoding.compare("UTF-8", Qt::CaseInsensitive) != 0) {
        const auto binary = readBoundedCompilerBinary(executable, cancelled);
        result.preparation = binary.preparation;
        if (!result.preparation.ready()) return result;
        result.facts.compilerUsingUTF8 = peImportsUnicodeArguments(binary.bytes) || executableHasUtf8Manifest(executable);
    }
#endif
    if (toolchain.compilerType != CompilerType::GCC) return result;
    ProcessProbeRequest request;
    request.executable = executable; request.arguments = {QStringLiteral("-v")};
    request.workingDirectory = QFileInfo(executable).absolutePath();
    request.environment = configuration.environment;
    request.environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    request.environment.insert(QStringLiteral("LANGUAGE"), {});
    const auto probe = runBoundedProcessProbe(request, cancelled);
    result.ownership = probe.ownership;
    result.preparation = probe.preparation;
    if (!result.preparation.ready()) return result;
    const int start = probe.output.indexOf("Configured with: ");
    const int end = start < 0 ? -1 : probe.output.indexOf('\n', start);
    const auto flags = start < 0 ? QByteArray{} : probe.output.mid(start, end < 0 ? -1 : end - start);
    result.facts.supportNLS = flags.contains("--enable-nls") && !flags.contains("--disable-nls");
#ifdef Q_OS_WIN
    result.facts.supportConvertingCharset = flags.contains("--with-libiconv") && !flags.contains("--with-libiconv=no");
#else
    result.facts.supportConvertingCharset = true;
#endif
    return result;
}
} // namespace Compilation
#endif
