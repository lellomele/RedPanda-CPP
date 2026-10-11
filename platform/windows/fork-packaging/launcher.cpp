// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// Native launcher: keep IDE DLLs inside app/, without adding them to child PATH.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace {
class Handle {
public:
    explicit Handle(HANDLE value) noexcept : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_;
};

std::wstring quote(const std::wstring& argument) {
    std::wstring result(1, L'"');
    std::size_t slashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') { ++slashes; continue; }
        if (character == L'"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring systemMessage(DWORD code) {
    wchar_t* text = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    const std::wstring result = length && text ? std::wstring(text, length) : L"Errore " + std::to_wstring(code);
    if (text) LocalFree(text);
    return result;
}

int showError(const std::wstring& message, DWORD code) {
    const std::wstring detail = message + L"\n\n" + systemMessage(code);
    MessageBoxW(nullptr, detail.c_str(), L"Red Panda C++", MB_OK | MB_ICONERROR);
    return 1;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        std::vector<wchar_t> filename(32768, L'\0');
        const DWORD count = GetModuleFileNameW(nullptr, filename.data(), static_cast<DWORD>(filename.size()));
        if (count == 0 || count >= filename.size()) return showError(L"Impossibile individuare la cartella dell'IDE.", GetLastError());
        const std::wstring path(filename.data(), count);
        const auto separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos) return showError(L"Percorso dell'IDE non valido.", ERROR_PATH_NOT_FOUND);
        const std::wstring program = path.substr(0, separator) + L"\\app\\RedPandaIDE-core.exe";
        const DWORD attributes = GetFileAttributesW(program.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
            return showError(L"Manca app\\RedPandaIDE-core.exe. Estrarre il pacchetto completo o reinstallare l'IDE.", ERROR_FILE_NOT_FOUND);
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) return showError(L"Impossibile leggere gli argomenti di avvio.", GetLastError());
        std::wstring command = quote(program);
        try {
            for (int index = 1; index < argc; ++index) command += L" " + quote(argv[index]);
        } catch (...) { LocalFree(argv); throw; }
        LocalFree(argv);
        if (command.size() >= 32767) return showError(L"Gli argomenti di avvio sono troppo lunghi.", ERROR_BAD_LENGTH);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION information{};
        // Preserve caller working directory and environment. Qt DLLs remain private.
        if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            nullptr, &startup, &information))
            return showError(L"Impossibile avviare Red Panda C++.", GetLastError());
        Handle thread(information.hThread), process(information.hProcess);
        if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0)
            return showError(L"Impossibile attendere la chiusura dell'IDE.", GetLastError());
        DWORD exitCode = 1;
        if (!GetExitCodeProcess(process.get(), &exitCode)) return showError(L"Impossibile leggere l'esito dell'IDE.", GetLastError());
        return static_cast<int>(exitCode);
    } catch (const std::exception&) {
        return showError(L"Risorse insufficienti per avviare l'IDE. Chiudere alcune applicazioni e riprovare.", ERROR_NOT_ENOUGH_MEMORY);
    }
}
