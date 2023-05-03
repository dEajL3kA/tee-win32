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

#define BUFFSIZE 4096U
#pragma warning(disable: 4706)

 // --------------------------------------------------------------------------
 // Utilities
 // --------------------------------------------------------------------------

static BOOL is_terminal(const HANDLE handle)
{
    DWORD mode;
    return GetConsoleMode(handle, &mode);
}

#define APPEND_STRING(X) \
    do { lstrcpyW(ptr, str##X); ptr += len##X; } while(0)

static wchar_t *concat_3(const wchar_t *const strA, const wchar_t *const strB, const wchar_t *const strC)
{
    const size_t lenA = lstrlenW(strA), lenB = lstrlenW(strB), lenC = lstrlenW(strC);
    wchar_t *const buffer = (wchar_t*) LocalAlloc(LPTR, sizeof(wchar_t) * (lenA + lenB + lenC + 1U));
    if (buffer)
    {
        wchar_t *ptr = buffer;
        APPEND_STRING(A);
        APPEND_STRING(B);
        APPEND_STRING(C);
    }
    return buffer;
}

#define SAFE_CLOSE_HANDLE(HANDLE) do \
{ \
    if (HANDLE) \
    { \
        CloseHandle((HANDLE)); \
    } \
} \
while (0)

// --------------------------------------------------------------------------
// Console CTRL+C handler
// --------------------------------------------------------------------------

static volatile BOOL g_stop = FALSE;

static BOOL WINAPI console_handler(const DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        g_stop = TRUE;
        return TRUE;
    default:
        return FALSE;
    }
}

// --------------------------------------------------------------------------
// Version
// --------------------------------------------------------------------------

static ULONGLONG get_version(void)
{
    const HRSRC hVersion = FindResourceW(NULL, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
    if (hVersion)
    {
        const HGLOBAL hResource = LoadResource(NULL, hVersion);
        if (hResource)
        {
            const DWORD sizeOfResource = SizeofResource(NULL, hResource);
            if (sizeOfResource >= sizeof(VS_FIXEDFILEINFO))
            {
                const PVOID addrResourceBlock = LockResource(hResource);
                if (addrResourceBlock)
                {
                    VS_FIXEDFILEINFO *fileInfoData;
                    UINT fileInfoSize;
                    if (VerQueryValueW(addrResourceBlock, L"\\", &fileInfoData, &fileInfoSize))
                    {
                        ULARGE_INTEGER fileVersion;
                        fileVersion.LowPart  = fileInfoData->dwFileVersionLS;
                        fileVersion.HighPart = fileInfoData->dwFileVersionMS;
                        return fileVersion.QuadPart;
                    }
                }
            }
        }
    }

    return 0U;
}

static const wchar_t *get_version_string(void)
{
    static wchar_t text[64U] = { '\0' };
    lstrcpyW(text, L"tee for Windows v#.#.# [" TEXT(__DATE__) L"]\n");

    const ULONGLONG version = get_version();
    if (version)
    {
        text[17U] = L'0' + ((version >> 48) & 0xFFFF);
        text[19U] = L'0' + ((version >> 32) & 0xFFFF);
        text[21U] = L'0' + ((version >> 16) & 0xFFFF);
    }

    return text;
}

// --------------------------------------------------------------------------
// Text output
// --------------------------------------------------------------------------

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

// --------------------------------------------------------------------------
// Writer thread
// --------------------------------------------------------------------------

static BYTE buffer[2U][BUFFSIZE];
static DWORD bytesTotal[2U] = { 0U, 0U };
static volatile ULONG_PTR index = 0U;

typedef struct
{
    HANDLE hOutput,hError;
    BOOL flush;
    HANDLE hEventReady[2U], hEventCompleted;
}
thread_t;

static DWORD WINAPI writer_thread_start_routine(const LPVOID lpThreadParameter)
{
    DWORD bytesWritten;
    const thread_t *const param = (thread_t*) lpThreadParameter;

    for (;;)
    {
        switch (WaitForMultipleObjects(2U, param->hEventReady, FALSE, INFINITE))
        {
        case WAIT_OBJECT_0:
            break;
        case WAIT_OBJECT_0 + 1U:
            SetEvent(param->hEventCompleted);
            return 0U;
        default:
            write_text(param->hError, L"[tee] System error: Failed to wait for event!\n");
            return 1U;
        }

        const ULONG_PTR myIndex = index;

        for (DWORD offset = 0U; offset < bytesTotal[myIndex]; offset += bytesWritten)
        {
            const BOOL result = WriteFile(param->hOutput, buffer[myIndex] + offset, bytesTotal[myIndex] - offset, &bytesWritten, NULL);
            if ((!result) || (!bytesWritten))
            {
                write_text(param->hError, L"[tee] Error: Not all data could be written!\n");
                break;
            }
        }

        SetEvent(param->hEventCompleted);

        if (param->flush)
        {
            FlushFileBuffers(param->hOutput);
        }
    }
}

// --------------------------------------------------------------------------
// MAIN
// --------------------------------------------------------------------------

int wmain(const int argc, const wchar_t *const argv[])
{
    HANDLE hThreads[2U] = { NULL, NULL };
    HANDLE hEventStop = NULL, hEventThrdReady[2U] = { NULL, NULL }, hEventCompleted[2U] = { NULL, NULL };
    HANDLE hMyFile = INVALID_HANDLE_VALUE;
    int exitCode = 1, argOff = 1;
    BOOL append = FALSE, flush = FALSE, ignore = FALSE;
    thread_t threadData[2U];

    /* Initialize standard streams */
    const HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE), hStdOut = GetStdHandle(STD_OUTPUT_HANDLE), hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    if ((hStdIn == INVALID_HANDLE_VALUE) || (hStdOut == INVALID_HANDLE_VALUE) || (hStdErr == INVALID_HANDLE_VALUE))
    {
        FatalExit(-1);
    }

    /* Set up CRTL+C handler */
    SetConsoleCtrlHandler(console_handler, TRUE);

    /* Print version */
    if ((argc > 1) && (lstrcmpiW(argv[1], L"--version") == 0))
    {
        write_text(hStdErr, get_version_string());
        return 0;
    }

    /* Print manpage */
    if ((argc < 2) || (lstrcmpiW(argv[1], L"/?") == 0) || (lstrcmpiW(argv[1], L"--help") == 0))
    {
        write_text(hStdErr, get_version_string());
        write_text(hStdErr, L"\n"
            L"Usage:\n"
            L"  your_program.exe [...] | tee.exe [options] <output_file>\n\n"
            L"Options:\n"
            L"  --append   Append to the existing file, instead of truncating\n"
            L"  --flush    Flush output file after each write operation\n"
            L"  --ignore   Ignore the interrupt signal (SIGINT), e.g. CTRL+C\n\n");
        return 1;
    }

    /* Parse command-line options */
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
            else if (lstrcmpiW(option, L"ignore") == 0)
            {
                ignore = TRUE;
            }
            else
            {
                wchar_t *const message = concat_3(L"[tee] Error: Invalid option \"--", option, L"\" encountered!\n");
                if (message)
                {
                    write_text(hStdErr, message);
                    LocalFree(message);
                }
                return 1;
            }
        }
        else
        {
            break; /* stop option processing */
        }
    }

    /* Check output file name */
    if (argOff >= argc)
    {
        write_text(hStdErr, L"[tee] Error: Output file name is missing!\n");
        return 1;
    }

    /* Create events */
    if (!(hEventStop = CreateEventW(NULL, TRUE, FALSE, NULL)))
    {
        write_text(hStdErr, L"[tee] System error: Failed to create event!\n\n");
        goto cleanup;
    }
    for (size_t i = 0U; i < 2U; ++i)
    {
        if (!(hEventThrdReady[i] = CreateEventW(NULL, FALSE, FALSE, NULL)))
        {
            write_text(hStdErr, L"[tee] System error: Failed to create event!\n\n");
            goto cleanup;
        }
        if (!(hEventCompleted[i] = CreateEventW(NULL, FALSE, FALSE, NULL)))
        {
            write_text(hStdErr, L"[tee] System error: Failed to create event!\n\n");
            goto cleanup;
        }
    }

    /* Open output file */
    if ((hMyFile = CreateFileW(argv[argOff], GENERIC_WRITE, FILE_SHARE_READ, NULL, append ? OPEN_ALWAYS : CREATE_ALWAYS, 0U, NULL)) == INVALID_HANDLE_VALUE)
    {
        write_text(hStdErr, L"[tee] Error: Failed to open the output file for writing!\n");
        goto cleanup;
    }

    /* Seek to the end of the file */
    if (append)
    {
        LARGE_INTEGER offset = { .QuadPart = 0LL };
        if (!SetFilePointerEx(hMyFile, offset, NULL, FILE_END))
        {
            write_text(hStdErr, L"[tee] Error: Failed to move the file pointer to the end of the file!\n");
            goto cleanup;
        }
    }

    /* Set up thread data */
    for (size_t i = 0; i < 2U; ++i)
    {
        threadData[i].hOutput = (i > 0U) ? hMyFile : hStdOut;
        threadData[i].hError = hStdErr;
        threadData[i].flush = flush && (!is_terminal(threadData[i].hOutput));
        threadData[i].hEventReady[0U] = hEventThrdReady[i];
        threadData[i].hEventReady[1U] = hEventStop;
        threadData[i].hEventCompleted = hEventCompleted[i];
    }

    /* Start threads */
    for (size_t i = 0; i < 2U; ++i)
    {
        if (!(hThreads[i] = CreateThread(NULL, 0U, writer_thread_start_routine, &threadData[i], 0U, NULL)))
        {
            write_text(hStdErr, L"[tee] System error: Failed to create thread!\n");
            goto cleanup;
        }
    }

    /* Are we reading from a pipe? */
    const BOOL isPipeInput = (GetFileType(hStdIn) == FILE_TYPE_PIPE);

    /* Initialize index */
    ULONG_PTR myIndex = 1U - index;

    /* Process all input from STDIN stream */
    do
    {
        for (size_t i = 0U; i < 2U; ++i)
        {
            if (!SetEvent(hEventThrdReady[i]))
            {
                write_text(hStdErr, L"[tee] System error: Failed to signal event!\n");
                goto cleanup;
            }
        }

        if (!ReadFile(hStdIn, buffer[myIndex], BUFFSIZE, &bytesTotal[myIndex], NULL))
        {
            if (GetLastError() != ERROR_BROKEN_PIPE)
            {
                write_text(hStdErr, L"[tee] Error: Failed to read input data!\n");
                goto cleanup;
            }
            break;
        }

        if ((!bytesTotal[myIndex]) && (!isPipeInput)) /*pipes may return zero bytes, even when more data can become available later!*/
        {
            break;
        }

        const DWORD waitResult = WaitForMultipleObjects(2U, hEventCompleted, TRUE, INFINITE);
        if ((waitResult != WAIT_OBJECT_0) && (waitResult != WAIT_OBJECT_0 + 1U))
        {
            write_text(hStdErr, L"[tee] System error: Failed to wait for events!\n");
            goto cleanup;
        }

        myIndex = (ULONG_PTR) InterlockedExchangePointer((PVOID*)&index, (PVOID)myIndex);
    }
    while ((!g_stop) || ignore);

    exitCode = 0;

cleanup:

    /* Stop the worker threads */
    if (hEventStop)
    {
        SetEvent(hEventStop);
    }

    /* Wait for worker threads to exit */
    if (hThreads[0U])
    {
        const DWORD waitResult = WaitForMultipleObjects(hThreads[1U] ? 2U : 1U, hThreads, TRUE, 12000U);
        if ((waitResult != WAIT_OBJECT_0) && (waitResult != WAIT_OBJECT_0 + 1U))
        {
            for (DWORD i = 0U; i < (hThreads[1U] ? 2U : 1U); ++i)
            {
                if (WaitForSingleObject(hThreads[i], 125U) != WAIT_OBJECT_0)
                {
                    write_text(hStdErr, L"[tee] Error: Worker thread did not exit cleanly!\n");
                    TerminateThread(hThreads[i], 1U);
                }
            }
        }
    }

    /* Close worker threads */
    for (DWORD i = 0U; i < 2U; ++i)
    {
        SAFE_CLOSE_HANDLE(hThreads[i]);
    }

    /* Close output file */
    if (hMyFile != INVALID_HANDLE_VALUE)
    {
        if (flush)
        {
            FlushFileBuffers(hMyFile);
        }
        CloseHandle(hMyFile);
    }

    /* Close events */
    for (size_t i = 0U; i < 2U; ++i)
    {
        SAFE_CLOSE_HANDLE(hEventThrdReady[i]);
        SAFE_CLOSE_HANDLE(hEventCompleted[i]);
    }
    SAFE_CLOSE_HANDLE(hEventStop);

    /* Exit */
    return exitCode;
}

// --------------------------------------------------------------------------
// CRT Startup
// --------------------------------------------------------------------------

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
