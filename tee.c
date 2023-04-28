/*
 * CertViewer - tee for Windows
 * Copyright (c) 2023 "dEajL3kA" <Cumpoing79@web.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sub license, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions: The above copyright notice and this
 * permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include <ShellAPI.h>

#define HANDLE_WRITE_ERROR(OFFSET) do \
{ \
    write_text(hStdErr, L"[tee] Error: Not all data could be written!\n\n"); \
    OFFSET = MAXDWORD; \
} \
while (0)

#define WRITE_DATA(DESTINATION, OFFSET) do \
{ \
    if ((OFFSET) < bytesRead) \
    { \
        if (WriteFile((DESTINATION), buffer + (OFFSET), bytesRead - (OFFSET), &bytesWritten, NULL)) \
        { \
            if (bytesWritten > 0U) \
            { \
                OFFSET += bytesWritten; \
            } \
            else \
            { \
                HANDLE_WRITE_ERROR(OFFSET); \
            } \
        } \
        else \
        { \
            HANDLE_WRITE_ERROR(OFFSET); \
        } \
    } \
} \
while (0)

#define BUFFSIZE 4096U

static volatile BOOL g_stopping = FALSE;

static BOOL WINAPI console_handler(const DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        g_stopping = TRUE;
        return TRUE;
    default:
        return FALSE;
    }
}

static char *utf16_to_utf8(const wchar_t *const input)
{
    const int buff_size = WideCharToMultiByte(CP_UTF8, 0, input, -1, NULL, 0, NULL, NULL);
    if (buff_size > 0)
    {
        char *const buffer = (char*)LocalAlloc(LPTR, buff_size);
        if (buffer)
        {
            const int result = WideCharToMultiByte(CP_UTF8, 0, input, -1, buffer, buff_size, NULL, NULL);
            if ((result > 0) && (result <= buff_size))
            {
                return buffer;
            }
            LocalFree(buffer);
        }
    }
    return NULL;
}

static BOOL write_text(const HANDLE handle, const wchar_t *const text)
{
    BOOL result = FALSE;
    DWORD written;
    if (GetConsoleMode(handle, &written))
    {
        result = WriteConsoleW(handle, text, lstrlenW(text), &written, NULL);
    }
    else
    {
        char *const utf8_text = utf16_to_utf8(text);
        if (utf8_text)
        {
            result = WriteFile(handle, utf8_text, lstrlenA(utf8_text), &written, NULL);
            LocalFree(utf8_text);
        }
    }
    return result;
}

int wmain(const int argc, const wchar_t *const argv[])
{
    SetConsoleCtrlHandler(console_handler, TRUE);

    const HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE), hStdOut = GetStdHandle(STD_OUTPUT_HANDLE), hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    if ((hStdIn == INVALID_HANDLE_VALUE) || (hStdOut == INVALID_HANDLE_VALUE) || (hStdErr == INVALID_HANDLE_VALUE))
    {
        FatalExit(-1);
    }

    if ((argc < 2) || (lstrcmpiW(argv[1], L"/?") == 0) || (lstrcmpiW(argv[1], L"--help") == 0))
    {
        write_text(hStdErr, L"tee for Windows [" TEXT(__DATE__) L"]\n\n");
        write_text(hStdErr, L"Usage:\n");
        write_text(hStdErr, L"  your_program.exe [options] | tee.exe [--append] [--flush] <output_file>\n\n");
        return 1;
    }

    BOOL append = FALSE, flush = FALSE;
    int argOff = 1;

    while (argOff < argc)
    {
        if ((argv[argOff][0U] == L'-') && (argv[argOff][1U] == L'-'))
        {
            const wchar_t *const option = argv[argOff++] + 2U;
            if (*option == L'\0')
            {
                break;
            }
            else if (lstrcmpiW(option, L"append") == 0)
            {
                append = TRUE;
            }
            else if (lstrcmpiW(option, L"flush") == 0)
            {
                flush = TRUE;
            }
            else
            {
                write_text(hStdErr, L"[tee] Error: Invalid option \"--");
                write_text(hStdErr, option);
                write_text(hStdErr, L"\" encountered!\n\n");
                return 1;
            }
        }
        else
        {
            break; /*stop option processing*/
        }
    }

    if (argOff >= argc)
    {
        write_text(hStdErr, L"[tee] Error: Output file name is missing!\n\n");
        return 1;
    }

    const HANDLE hFile = CreateFileW(argv[argOff], GENERIC_WRITE, FILE_SHARE_READ, NULL, append ? OPEN_ALWAYS : CREATE_ALWAYS, 0U, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        write_text(hStdErr, L"[tee] Error: Failed to open the output file for writing!\n\n");
        return -1;
    }

    if (append)
    {
        LARGE_INTEGER offset = { .QuadPart = 0LL };
        if (!SetFilePointerEx(hFile, offset, NULL, FILE_END))
        {
            write_text(hStdErr, L"[tee] Error: Failed to move the file pointer to the end of the file!\n\n");
            CloseHandle(hFile);
            return -1;
        }
    }

    static BYTE buffer[BUFFSIZE];
    DWORD bytesRead, bytesWritten, offsetStdOut, offsetFile;

    while (!g_stopping)
    {
        if (!ReadFile(hStdIn, buffer, BUFFSIZE, &bytesRead, NULL))
        {
            goto cleanup;
        }

        offsetStdOut = offsetFile = 0U;

        while ((offsetStdOut < bytesRead) || (offsetFile < bytesRead))
        {
            WRITE_DATA(hStdOut, offsetStdOut);
            WRITE_DATA(hFile, offsetFile);
            if (flush)
            {
                FlushFileBuffers(hFile);
            }
        }
    }

cleanup:

    FlushFileBuffers(hFile);
    CloseHandle(hFile);

    return 0;
}

#pragma warning(disable: 4702)

int wmainCRTStartup(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS);

    int nArgs;
    LPWSTR *const szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (!szArglist)
    {
        FatalExit(-1);
    }

    const int retval = wmain(nArgs, szArglist);
    LocalFree(szArglist);
    ExitProcess((UINT)retval);

    return 0;
}
