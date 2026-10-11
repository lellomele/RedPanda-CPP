#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <limits.h>
#include "resource.h"

namespace {
HWND mainDialog = nullptr;
HWND passwordEdit = nullptr;
WNDPROC previousPasswordProc = nullptr;
int exitCode = 1; // Cancel and failures never return an empty successful reply.

void clearAndFree(void *buffer, SIZE_T size)
{
    if (buffer) {
        SecureZeroMemory(buffer, size);
        LocalFree(buffer);
    }
}

bool encodeUtf8(const wchar_t *text, int length, char *&bytes, DWORD &size)
{
    bytes = nullptr;
    size = 0;
    if (length == 0)
        return true;
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                             text, length, nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        return false;
    char *encoded = static_cast<char *>(LocalAlloc(LMEM_FIXED, required));
    if (!encoded)
        return false;
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                            text, length, encoded, required, nullptr, nullptr);
    if (written != required) {
        clearAndFree(encoded, required);
        return false;
    }
    bytes = encoded;
    size = static_cast<DWORD>(written);
    return true;
}

bool submitPassword()
{
    SetLastError(ERROR_SUCCESS);
    const int length = GetWindowTextLengthW(passwordEdit);
    if (length < 0 || length == INT_MAX || (length == 0 && GetLastError() != ERROR_SUCCESS))
        return false;
    const SIZE_T wideSize = (static_cast<SIZE_T>(length) + 1) * sizeof(wchar_t);
    wchar_t *password = static_cast<wchar_t *>(LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, wideSize));
    if (!password)
        return false;
    const int copied = GetWindowTextW(passwordEdit, password, length + 1);
    char *encoded = nullptr;
    DWORD encodedSize = 0;
    bool success = copied == length && encodeUtf8(password, copied, encoded, encodedSize);
    clearAndFree(password, wideSize);
    if (success) {
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        success = output != nullptr && output != INVALID_HANDLE_VALUE;
        DWORD offset = 0;
        while (success && offset < encodedSize) {
            DWORD written = 0;
            success = WriteFile(output, encoded + offset, encodedSize - offset, &written, nullptr)
                    && written > 0;
            offset += written;
        }
    }
    clearAndFree(encoded, encodedSize);
    return success;
}

void closeDialog(HWND dialog, bool submit)
{
    if (submit)
        exitCode = submitPassword() ? 0 : 1;
    else
        exitCode = 1;
    if (passwordEdit && IsWindow(passwordEdit))
        SetWindowTextW(passwordEdit, L"");
    DestroyWindow(dialog);
}

LRESULT CALLBACK passwordProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessageW(mainDialog, WM_COMMAND, IDOK, 0);
        return 0;
    }
    return CallWindowProcW(previousPasswordProc, window, message, wParam, lParam);
}

INT_PTR CALLBACK dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM)
{
    switch (message) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            closeDialog(dialog, LOWORD(wParam) == IDOK);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        closeDialog(dialog, false);
        return TRUE;
    case WM_DESTROY:
        PostQuitMessage(exitCode);
        return TRUE;
    }
    return FALSE;
}

bool encodingSelfTest()
{
    // Synthetic data only. Never print this payload or any entered credential.
    const wchar_t sample[]{L'Q', L'A', L'-', 0x00e0, 0x20ac, 0xd83d, 0xdc3c};
    const unsigned char expected[]{'Q', 'A', '-', 0xc3, 0xa0, 0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x90, 0xbc};
    char *bytes = nullptr;
    DWORD size = 0;
    bool ok = encodeUtf8(sample, sizeof(sample) / sizeof(sample[0]), bytes, size);
    ok = ok && size == sizeof(expected) && memcmp(bytes, expected, sizeof(expected)) == 0;
    clearAndFree(bytes, size);
    const wchar_t malformed[]{0xd800};
    const bool malformedAccepted = encodeUtf8(malformed, 1, bytes, size);
    ok = ok && !malformedAccepted && bytes == nullptr && size == 0;
    clearAndFree(bytes, size);
    const bool emptyAccepted = encodeUtf8(L"", 0, bytes, size);
    ok = ok && emptyAccepted && bytes == nullptr && size == 0;
    clearAndFree(bytes, size);
    return ok;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int show)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool selfTest = argv && argc == 2 && wcscmp(argv[1], L"--self-test") == 0;
    if (argv)
        LocalFree(argv);
    if (selfTest) {
        const bool success = encodingSelfTest();
        fputs(success ? "Askpass encoding checks passed\n" : "Askpass encoding checks failed\n", stdout);
        return success ? 0 : 1;
    }
    mainDialog = CreateDialogW(instance, MAKEINTRESOURCEW(IDD_MAIN_DIALOG), nullptr, dialogProc);
    if (!mainDialog)
        return 1;
    passwordEdit = GetDlgItem(mainDialog, ID_TXT_PASSWORD);
    if (!passwordEdit || !GetDlgItem(mainDialog, ID_TXT_PROMPT)) {
        DestroyWindow(mainDialog);
        return 1;
    }
    SetLastError(ERROR_SUCCESS);
    previousPasswordProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            passwordEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(passwordProc)));
    if (!previousPasswordProc) {
        DestroyWindow(mainDialog);
        return 1;
    }
    SetDlgItemTextW(mainDialog, ID_TXT_PROMPT, commandLine);
    SendMessageW(passwordEdit, EM_SETPASSWORDCHAR, L'\x2022', 0);
    const HICON icon = LoadIconW(instance, L"A");
    if (icon) {
        SendMessageW(mainDialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SendMessageW(mainDialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    }
    ShowWindow(mainDialog, show);
    SetFocus(passwordEdit);
    MSG message{};
    for (;;) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            if (IsWindow(mainDialog))
                DestroyWindow(mainDialog);
            return 1;
        }
        if (result == 0)
            break;
        if (!IsWindow(mainDialog) || !IsDialogMessageW(mainDialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return exitCode;
}
