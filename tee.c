/*
 * tee for Windows
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
#include <intrin.h>
#include <stdarg.h>
#include "include/cpu.h"
#include "include/version.h"

#pragma intrinsic(_InterlockedCompareExchange, _InterlockedDecrement)

#define BUFF_SIZE (PROCESSOR_BITNESS * 128U)
#define BUFFERS 3U
#define MAX_THREADS MAXIMUM_WAIT_OBJECTS

// --------------------------------------------------------------------------
// Assertions
// --------------------------------------------------------------------------

#ifndef NDEBUG
#define ASSERT(CONDIATION, HANDLE_OUT, MESSAGE) do { \
    static const wchar_t *const _message = L"[tee] Assertion Failed: " MESSAGE L"\n"; \
    if (!(CONDIATION)) { \
        write_text((HANDLE_OUT), _message); \
        FatalExit(-1); \
    } \
} while(0)
#else
#define ASSERT(CONDIATION, HANDLE_OUT, MESSAGE) ((void)0)
#endif

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

static DWORD count_handles(const HANDLE *const array, const size_t maximum)
{
    DWORD counter;
    for (counter = 0U; counter < maximum; ++counter)
    {
        if (!array[counter])
        {
            break;
        }
    }

    return counter;
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
    if ((to_lower(filePath[0U]) == L'n') && (to_lower(filePath[1U]) == L'u') && (to_lower(filePath[2U]) == L'l'))
    {
        return ((filePath[3U] == L'\0') || (filePath[3U] == L'.'));
    }

    return FALSE;
}

static wchar_t *format_string(const wchar_t *const format, ...)
{
    wchar_t* buffer = NULL;
    va_list ap;

    va_start(ap, format);
    const DWORD result = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER, format, 0U, 0U, (LPWSTR)&buffer, 1U, &ap);
    va_end(ap);

    return result ? buffer : NULL;
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

#define CONCAT(...) concat_va(__VA_ARGS__, NULL)

#define VALID_HANDLE(HANDLE) (((HANDLE) != NULL) && ((HANDLE) != INVALID_HANDLE_VALUE))

#define FILL_ARRAY(ARRAY, VALUE) do \
{ \
    for (size_t _index = 0U; _index < ARRAYSIZE(ARRAY); ++_index) \
    { \
        ARRAY[_index] = (VALUE); \
    } \
} \
while (0)

#define CLOSE_HANDLE(HANDLE) do \
{ \
    if (VALID_HANDLE(HANDLE)) \
    { \
        CloseHandle((HANDLE)); \
        (HANDLE) = NULL; \
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

#define INCREMENT_INDEX(INDEX, FLAG) do { if (++(INDEX) >= BUFFERS) { (INDEX) = 0U; if (++(FLAG) > 2U) { (FLAG) = 1U; } } } while (0)

typedef struct _thread
{
    HANDLE hOutput, hError;
    BOOL flush;
}
thread_t;

static thread_t g_threadData[MAX_THREADS];
static BYTE g_buffer[BUFFERS][BUFF_SIZE], g_state[BUFF_SIZE] = { 0U, 0U, 0U };
static DWORD g_bytesTotal[BUFF_SIZE] = { 0U, 0U, 0U };
static volatile LONG g_pending[BUFF_SIZE] = { 0L, 0L, 0L };
static SRWLOCK g_rwLocks[BUFFERS];
static CONDITION_VARIABLE g_condIsReady[BUFFERS], g_condAllDone[BUFFERS];

static DWORD WINAPI writer_thread_start_routine(const LPVOID lpThreadParameter)
{
    DWORD bytesWritten = 0U, myIndex = 0U, pendingThreads = 0U;
    BYTE myFlag = 1U;
    BOOL writeErrors = FALSE;
    const thread_t *const param = &g_threadData[(DWORD_PTR)lpThreadParameter];

    for (;;)
    {
        ASSERT(myIndex < BUFFERS, param->hError, L"Current buffer index is out of range!");

        AcquireSRWLockShared(&g_rwLocks[myIndex]);

        while (!(g_state[myIndex] & myFlag))
        {
            if (!SleepConditionVariableSRW(&g_condIsReady[myIndex], &g_rwLocks[myIndex], INFINITE, CONDITION_VARIABLE_LOCKMODE_SHARED))
            {
                ReleaseSRWLockShared(&g_rwLocks[myIndex]);
                write_text(param->hError, L"[tee] System error: Failed to sleep on the conditional variable!\n");
                return 1U;
            }
        }

        if (g_state[myIndex] > 2U)
        {
            ReleaseSRWLockShared(&g_rwLocks[myIndex]);
            if (writeErrors)
            {
                write_text(param->hError, L"[tee] Error: Not all data could be written!\n");
            }
            return 0U;
        }

        for (DWORD offset = 0U; offset < g_bytesTotal[myIndex]; offset += bytesWritten)
        {
            const BOOL result = WriteFile(param->hOutput, g_buffer[myIndex] + offset, g_bytesTotal[myIndex] - offset, &bytesWritten, NULL);
            if ((!result) || (!bytesWritten))
            {
                writeErrors = TRUE;
                break;
            }
        }

        ASSERT(g_pending > 0U, param->hError, L"Pending threads counter must be a positive value!");

        pendingThreads = _InterlockedDecrement(&g_pending[myIndex]);

        ReleaseSRWLockShared(&g_rwLocks[myIndex]);

        if (!pendingThreads)
        {
            WakeConditionVariable(&g_condAllDone[myIndex]);
        }

        INCREMENT_INDEX(myIndex, myFlag);

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
    BOOL append, delay, flush, help, ignore, version;
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
    PARSE_OPTION('d', delay);
    PARSE_OPTION('f', flush);
    PARSE_OPTION('h', help);
    PARSE_OPTION('i', ignore);
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
    HANDLE hThreads[MAX_THREADS], hMyFiles[MAX_THREADS - 1U];
    int exitCode = 1, argOff = 1;
    DWORD fileCount = 0U, threadCount = 0U, myIndex = 0U;
    options_t options;

    /* Initialize local variables */
    FILL_ARRAY(hMyFiles, INVALID_HANDLE_VALUE);
    FILL_ARRAY(hThreads, NULL);
    SecureZeroMemory(&options, sizeof(options_t));

    /* Initialize standard streams */
    const HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE), hStdOut = GetStdHandle(STD_OUTPUT_HANDLE), hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    if (!(VALID_HANDLE(hStdIn) && VALID_HANDLE(hStdOut) && VALID_HANDLE(hStdErr)))
    {
        if (VALID_HANDLE(hStdErr))
        {
            write_text(hStdErr, L"[tee] System error: Failed to initialize standard I/O handles!\n");
        }
        return -1;
    }

    /* Initialize read/write locks and condition variables */
    for (DWORD index = 0; index < BUFFERS; ++index)
    {
        InitializeSRWLock(&g_rwLocks[index]);
        InitializeConditionVariable(&g_condIsReady[index]);
        InitializeConditionVariable(&g_condAllDone[index]);
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

    /* Print manual page */
    if (options.help || options.version)
    {
        wchar_t *const versionString = format_string(L"tee for Windows v%1!u!.%2!u!.%3!u! [%4!s!] [%5!s!]\n", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH, PROCESSOR_ARCHITECTURE, TEXT(__DATE__));
        write_text(hStdErr, versionString ? versionString : L"tee for Windows\n");
        if (options.help)
        {
            write_text(hStdErr, L"\n"
                L"Copy standard input to output file(s), and also to standard output.\n\n"
                L"Usage:\n"
                L"  gizmo.exe [...] | tee.exe [options] <file_1> ... <file_n>\n\n"
                L"Options:\n"
                L"  -a --append  Append to the existing file, instead of truncating\n"
                L"  -f --flush   Flush output file after each write operation\n"
                L"  -i --ignore  Ignore the interrupt signal (SIGINT), e.g. CTRL+C\n"
                L"  -d --delay   Add a small delay after each read operation\n\n");
        }
        if (versionString)
        {
            LocalFree(versionString);
        }
        return 0;
    }

    /* Check output file name */
    if (argOff >= argc)
    {
        write_text(hStdErr, L"[tee] Error: Output file name is missing. Type \"tee --help\" for details!\n");
        return 1;
    }

    /* Determine input type */
    const DWORD inputType = GetFileType(hStdIn);
    if (inputType == FILE_TYPE_UNKNOWN)
    {
        if (GetLastError() != NO_ERROR)
        {
            write_text(hStdErr, L"[tee] System error: Failed to initialize standard input stream!\n");
            return -1;
        }
    }

    /* Validate output stream */
    if (GetFileType(hStdOut) == FILE_TYPE_UNKNOWN)
    {
        if (GetLastError() != NO_ERROR)
        {
            write_text(hStdErr, L"[tee] System error: Failed to initialize standard output stream!\n");
            return -1;
        }
    }

    /* Open output file(s) */
    while ((argOff < argc) && (fileCount < ARRAYSIZE(hMyFiles)))
    {
        const wchar_t* const fileName = argv[argOff++];
        if (!is_null_device(fileName))
        {
            const HANDLE hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, options.append ? OPEN_ALWAYS : CREATE_ALWAYS, 0U, NULL);
            if ((hMyFiles[fileCount++] = hFile) == INVALID_HANDLE_VALUE)
            {
                WRITE_TEXT(L"[tee] Error: Failed to open the output file \"", fileName, L"\" for writing!\n");
                goto cleanup;
            }
            else if (options.append)
            {
                LARGE_INTEGER offset = { .QuadPart = 0LL };
                if (!SetFilePointerEx(hFile, offset, NULL, FILE_END))
                {
                    write_text(hStdErr, L"[tee] Error: Failed to move the file pointer to the end of the file!\n");
                    goto cleanup;
                }
            }
        }
    }

    /* Check output file name */
    if (argOff < argc)
    {
        write_text(hStdErr, L"[tee] Warning: Too many input files, ignoring excess files!\n");
    }

    /* Determine number of outputs */
    const DWORD outputCount = fileCount + 1U;

    /* Start threads */
    for (DWORD threadId = 0; threadId < outputCount; ++threadId)
    {
        g_threadData[threadId].hOutput = (threadId > 0U) ? hMyFiles[threadId - 1U] : hStdOut;
        g_threadData[threadId].hError = hStdErr;
        g_threadData[threadId].flush = options.flush && (!is_terminal(g_threadData[threadId].hOutput));
        if (!(hThreads[threadCount++] = CreateThread(NULL, 0U, writer_thread_start_routine, (LPVOID)(DWORD_PTR)threadId, 0U, NULL)))
        {
            write_text(hStdErr, L"[tee] System error: Failed to create thread!\n");
            goto cleanup;
        }
    }

    /* Initialize the index */
    BYTE myFlag = 1U;

    /* Process all input from STDIN stream */
    do
    {
        ASSERT(myIndex < BUFFERS, hStdErr, L"Current buffer index is out of range!");

        AcquireSRWLockExclusive(&g_rwLocks[myIndex]);

        while (_InterlockedCompareExchange(&g_pending[myIndex], threadCount, 0L))
        {
            if (!SleepConditionVariableSRW(&g_condAllDone[myIndex], &g_rwLocks[myIndex], INFINITE, 0U))
            {
                ReleaseSRWLockExclusive(&g_rwLocks[myIndex]);
                write_text(hStdErr, L"[tee] System error: Failed to sleep on the conditional variable!\n");
                goto cleanup;
            }
        }

        if (!ReadFile(hStdIn, g_buffer[myIndex], BUFF_SIZE, &g_bytesTotal[myIndex], NULL))
        {
            const DWORD error = GetLastError();
            ReleaseSRWLockExclusive(&g_rwLocks[myIndex]);
            if (error != ERROR_BROKEN_PIPE)
            {
                write_text(hStdErr, L"[tee] Error: Failed to read input data!\n");
                goto cleanup;
            }
            break;
        }

        if (!g_bytesTotal[myIndex])
        {
            ReleaseSRWLockExclusive(&g_rwLocks[myIndex]);
            if (inputType == FILE_TYPE_PIPE)
            {
                continue; /*pipes may return zero bytes, even when more data can become available later!*/
            }
            break;
        }

        g_state[myIndex] = myFlag;

        ReleaseSRWLockExclusive(&g_rwLocks[myIndex]);
        WakeAllConditionVariable(&g_condIsReady[myIndex]);

        INCREMENT_INDEX(myIndex, myFlag);

        if (options.delay)
        {
            Sleep(1U);
        }
    }
    while ((!g_stop) || options.ignore);

    exitCode = 0;

cleanup:

    /* Stop the worker threads */
    AcquireSRWLockExclusive(&g_rwLocks[myIndex]);
    g_state[myIndex] = MAXBYTE;
    g_pending[myIndex] = MAX_THREADS;
    ReleaseSRWLockExclusive(&g_rwLocks[myIndex]);
    WakeAllConditionVariable(&g_condIsReady[myIndex]);

    /* Wait for worker threads to exit */
    const DWORD pendingThreads = count_handles(hThreads, ARRAYSIZE(hThreads));
    if (pendingThreads > 0U)
    {
        const DWORD result = WaitForMultipleObjects(pendingThreads, hThreads, TRUE, 10000U);
        if (!((result >= WAIT_OBJECT_0) && (result < WAIT_OBJECT_0 + pendingThreads)))
        {
            for (DWORD threadId = 0U; threadId < pendingThreads; ++threadId)
            {
                if (WaitForSingleObject(hThreads[threadId], 125U) != WAIT_OBJECT_0)
                {
                    write_text(hStdErr, L"[tee] Error: Worker thread did not exit cleanly!\n");
                    TerminateThread(hThreads[threadId], 1U);
                }
            }
        }
    }

    /* Flush the output file */
    if (options.flush)
    {
        for (size_t fileIndex = 0U; fileIndex < ARRAYSIZE(hMyFiles); ++fileIndex)
        {
            if (hMyFiles[fileIndex] != INVALID_HANDLE_VALUE)
            {
                FlushFileBuffers(hMyFiles[fileIndex]);
            }
        }
    }

    /* Close worker threads */
    for (DWORD threadId = 0U; threadId < ARRAYSIZE(hThreads); ++threadId)
    {
        CLOSE_HANDLE(hThreads[threadId]);
    }

    /* Close the output file(s) */
    for (size_t fileIndex = 0U; fileIndex < ARRAYSIZE(hMyFiles); ++fileIndex)
    {
        CLOSE_HANDLE(hMyFiles[fileIndex]);
    }

    /* Exit */
    return exitCode;
}

// --------------------------------------------------------------------------
// CRT Startup
// --------------------------------------------------------------------------

#ifndef _DEBUG
#pragma warning(disable: 4702)

int _startup(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS);

    int nArgs;
    LPWSTR *const szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (!szArglist)
    {
        OutputDebugStringA("[tee-win32] System error: Failed to initialize command-line arguments!\n");
        ExitProcess((UINT)-1);
    }

    const int retval = wmain(nArgs, szArglist);
    LocalFree(szArglist);
    ExitProcess((UINT)retval);

    return 0;
}

#endif
