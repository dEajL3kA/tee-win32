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
#include <stdarg.h>

#pragma warning(disable: 4706)
#define BUFFSIZE 8192U

 // --------------------------------------------------------------------------
 // Utilities
 // --------------------------------------------------------------------------

static wchar_t to_lower(const wchar_t c)
{
    return ((c >= L'A') && (c <= L'Z')) ? (L'a' + (c - L'A')) : c;
}

static BOOL is_terminal(const HANDLE handle)
{
    DWORD mode;
    return GetConsoleMode(handle, &mode);
}

static const wchar_t *get_filename(const wchar_t *filePath)
{
    for (const wchar_t *ptr = filePath; *ptr != L'\0'; ++ptr)
    {
        if ((*ptr == L'\\') || (*ptr == L'/'))
        {
            filePath = ptr + 1U;
        }
    }
    return filePath;
}

static BOOL is_null_device(const wchar_t *filePath)
{
    filePath = get_filename(filePath);
    if ((to_lower(filePath[0U]) == L'n') && (to_lower(filePath[1U]) == L'u') || (to_lower(filePath[2U]) == L'l'))
    {
        return ((filePath[3U] == L'\0') || (filePath[3U] == L'.'));
    }
    return FALSE;
}

static wchar_t *concat_va(const wchar_t *const first, ...)
{
    const wchar_t *ptr;
    va_list ap;

    va_start(ap, first);
    size_t len = 0U;
    for (ptr = first; ptr != NULL; ptr = va_arg(ap, const wchar_t*))
    {
        len = lstrlenW(ptr);
    }
    va_end(ap);

    wchar_t *const buffer = (wchar_t*)LocalAlloc(LPTR, sizeof(wchar_t) * (len + 1U));
    if (buffer)
    {
        va_start(ap, first);
        for (ptr = first; ptr != NULL; ptr = va_arg(ap, const wchar_t*))
        {
            lstrcatW(buffer, ptr);
        }
        va_end(ap);
    }

    return buffer;
}

#define CLOSE_HANDLE(HANDLE) do \
{ \
    if (((HANDLE) != NULL) && ((HANDLE) != INVALID_HANDLE_VALUE)) \
    { \
        CloseHandle((HANDLE)); \
        (HANDLE) = NULL; \
    } \
} \
while (0)

#define CONCAT(...) concat_va(__VA_ARGS__, NULL)

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

#define WRITE_TEXT(...) do \
{ \
    wchar_t* const _message = CONCAT(__VA_ARGS__); \
    if (_message) \
    { \
        write_text(hStdErr, _message); \
        LocalFree(_message); \
    } \
} \
while (0)

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
// Options
// --------------------------------------------------------------------------

typedef struct
{
    BOOL append, flush, ignore, help, version;
}
options_t;

#define PARSE_OPTION(SHRT, NAME) do \
{ \
    if ((lc == L##SHRT) || (name && (lstrcmpiW(name, L#NAME) == 0))) \
    { \
        options->NAME = TRUE; \
        return TRUE; \
    } \
} \
while (0)

static BOOL parse_option(options_t *const options, const wchar_t c, const wchar_t *const name)
{
    const wchar_t lc = to_lower(c);

    PARSE_OPTION('a', append);
    PARSE_OPTION('f', flush);
    PARSE_OPTION('i', ignore);
    PARSE_OPTION('h', help);
    PARSE_OPTION('v', version);

    return FALSE;
}

static BOOL parse_argument(options_t *const options, const wchar_t *const argument)
{
    if ((argument[0U] != L'-') || (argument[1U] == L'\0'))
    {
        return FALSE;
    }

    if (argument[1U] == L'-')
    {
        return (argument[2U] != L'\0') && parse_option(options, L'\0', argument + 2U);
    }
    else
    {
        for (const wchar_t* ptr = argument + 1U; *ptr != L'\0'; ++ptr)
        {
            if (!parse_option(options, *ptr, NULL))
            {
                return FALSE;
            }
        }
        return TRUE;
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
    options_t options;
    thread_t threadData[2U];

    /* Clear options */
    SecureZeroMemory(&options, sizeof(options_t));

    /* Initialize standard streams */
    const HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE), hStdOut = GetStdHandle(STD_OUTPUT_HANDLE), hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    if ((hStdIn == INVALID_HANDLE_VALUE) || (hStdOut == INVALID_HANDLE_VALUE) || (hStdErr == INVALID_HANDLE_VALUE))
    {
        return -1;
    }

    /* Set up CRTL+C handler */
    SetConsoleCtrlHandler(console_handler, TRUE);

    /* Parse command-line options */
    while ((argOff < argc) && (argv[argOff][0U] == L'-') && (argv[argOff][1U] != L'\0'))
    {
        const wchar_t *const argValue= argv[argOff++];
        if ((argValue[1U] == L'-') && (argValue[2U] == L'\0'))
        {
            break; /*stop!*/
        }
        else if (!parse_argument(&options, argValue))
        {
            WRITE_TEXT(L"[tee] Error: Invalid option \"", argValue, L"\" encountered!\n");
            return 1;
        }
    }

    /* Print version information */
    if (options.version)
    {
        write_text(hStdErr, get_version_string());
        return 0;
    }

    /* Print manual page */
    if (options.help)
    {
        write_text(hStdErr, get_version_string());
        write_text(hStdErr, L"\n"
            L"Copy standard input to output file, and also to standard output.\n\n"
            L"Usage:\n"
            L"  gizmo.exe [...] | tee.exe [options] <output_file>\n\n"
            L"Options:\n"
            L"  -a --append  Append to the existing file, instead of truncating\n"
            L"  -f --flush   Flush output file after each write operation\n"
            L"  -i --ignore  Ignore the interrupt signal (SIGINT), e.g. CTRL+C\n\n");
        return 0;
    }

    /* Check output file name */
    if (argOff >= argc)
    {
        write_text(hStdErr, L"[tee] Error: Output file name is missing. Type \"tee --help\" for details!\n");
        return 1;
    }

    /* Check for excess arguments */
    if (argOff + 1 < argc)
    {
        write_text(hStdErr, L"[tee] Warning: Excess command line argument(s) ignored!\n");
    }

    /* Open output file */
    if (!is_null_device(argv[argOff]))
    {
        if ((hMyFile = CreateFileW(argv[argOff], GENERIC_WRITE, FILE_SHARE_READ, NULL, options.append ? OPEN_ALWAYS : CREATE_ALWAYS, 0U, NULL)) == INVALID_HANDLE_VALUE)
        {
            WRITE_TEXT(L"[tee] Error: Failed to open the output file \"", argv[argOff], L"\" for writing!\n");
            goto cleanup;
        }

        /* Seek to the end of the file */
        if (options.append)
        {
            LARGE_INTEGER offset = { .QuadPart = 0LL };
            if (!SetFilePointerEx(hMyFile, offset, NULL, FILE_END))
            {
                write_text(hStdErr, L"[tee] Error: Failed to move the file pointer to the end of the file!\n");
                goto cleanup;
            }
        }
    }

    /* Determine thread count */
    const DWORD threadCount = (hMyFile != INVALID_HANDLE_VALUE) ? 2U : 1U;

    /* Create events */
    if (!(hEventStop = CreateEventW(NULL, TRUE, FALSE, NULL)))
    {
        write_text(hStdErr, L"[tee] System error: Failed to create event object!\n\n");
        goto cleanup;
    }
    for (size_t threadId = 0U; threadId < threadCount; ++threadId)
    {
        if (!(hEventThrdReady[threadId] = CreateEventW(NULL, FALSE, FALSE, NULL)))
        {
            write_text(hStdErr, L"[tee] System error: Failed to create event object!\n\n");
            goto cleanup;
        }
        if (!(hEventCompleted[threadId] = CreateEventW(NULL, FALSE, FALSE, NULL)))
        {
            write_text(hStdErr, L"[tee] System error: Failed to create event object!\n\n");
            goto cleanup;
        }
    }

    /* Set up thread data */
    for (size_t threadId = 0; threadId < threadCount; ++threadId)
    {
        threadData[threadId].hOutput = (threadId > 0U) ? hMyFile : hStdOut;
        threadData[threadId].hError = hStdErr;
        threadData[threadId].flush = options.flush && (!is_terminal(threadData[threadId].hOutput));
        threadData[threadId].hEventReady[0U] = hEventThrdReady[threadId];
        threadData[threadId].hEventReady[1U] = hEventStop;
        threadData[threadId].hEventCompleted = hEventCompleted[threadId];
    }

    /* Start threads */
    for (DWORD threadId = 0; threadId < threadCount; ++threadId)
    {
        if (!(hThreads[threadId] = CreateThread(NULL, 0U, writer_thread_start_routine, &threadData[threadId], 0U, NULL)))
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
        for (DWORD threadId = 0U; threadId < threadCount; ++threadId)
        {
            if (!SetEvent(hEventThrdReady[threadId]))
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

        const DWORD waitResult = WaitForMultipleObjects(threadCount, hEventCompleted, TRUE, INFINITE);
        if ((waitResult != WAIT_OBJECT_0) && (waitResult != WAIT_OBJECT_0 + 1U))
        {
            write_text(hStdErr, L"[tee] System error: Failed to wait for events!\n");
            goto cleanup;
        }

        myIndex = (ULONG_PTR) InterlockedExchangePointer((PVOID*)&index, (PVOID)myIndex);
    }
    while ((!g_stop) || options.ignore);

    exitCode = 0;

cleanup:

    /* Stop the worker threads */
    if (hEventStop)
    {
        SetEvent(hEventStop);
    }

    /* Wait for worker threads to exit */
    const DWORD _threadCount = hThreads[0U] ? (hThreads[1U] ? 2U : 1U) : 0U;
    if (_threadCount)
    {
        const DWORD waitResult = WaitForMultipleObjects(_threadCount, hThreads, TRUE, 12000U);
        if ((waitResult != WAIT_OBJECT_0) && (waitResult != WAIT_OBJECT_0 + 1U))
        {
            for (DWORD threadId = 0U; threadId < _threadCount; ++threadId)
            {
                if (WaitForSingleObject(hThreads[threadId], 1U) != WAIT_OBJECT_0)
                {
                    write_text(hStdErr, L"[tee] Error: Worker thread did not exit cleanly!\n");
                    TerminateThread(hThreads[threadId], 1U);
                }
            }
        }
    }

    /* Flush the output file */
    if ((hMyFile != INVALID_HANDLE_VALUE) && options.flush)
    {
        FlushFileBuffers(hMyFile);
    }

    /* Close worker threads */
    for (DWORD threadId = 0U; threadId < 2U; ++threadId)
    {
        CLOSE_HANDLE(hThreads[threadId]);
    }

    /* Close the output file */
    CLOSE_HANDLE(hMyFile);

    /* Close events */
    for (DWORD threadId = 0U; threadId < 2U; ++threadId)
    {
        CLOSE_HANDLE(hEventThrdReady[threadId]);
        CLOSE_HANDLE(hEventCompleted[threadId]);
    }
    CLOSE_HANDLE(hEventStop);

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
        ExitProcess((UINT)-1);
    }

    const int retval = wmain(nArgs, szArglist);
    LocalFree(szArglist);
    ExitProcess((UINT)retval);

    return 0;
}
