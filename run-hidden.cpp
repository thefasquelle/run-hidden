
#include <windows.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// NEW: Forward declaration for the I/O forwarding thread
DWORD WINAPI IoForwardThread(LPVOID lpParam);

// NEW: Struct to pass data to the threads
struct THREAD_PARAMS {
    HANDLE hRead;
    HANDLE hWrite;
};

// NEW: Helper function to get parent's standard handles, attaching to console if needed.
void InitializeParentHandles(HANDLE& hParentStdIn, HANDLE& hParentStdOut, HANDLE& hParentStdErr)
{
    hParentStdIn = GetStdHandle(STD_INPUT_HANDLE);
    hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hParentStdErr = GetStdHandle(STD_ERROR_HANDLE);

    // If we can't get a valid output handle, it means we're not attached to a console
    // or redirected. Let's try to attach to the parent process's console.
    if (hParentStdOut == NULL || hParentStdOut == INVALID_HANDLE_VALUE)
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
        {
            // If we successfully attached, try getting the handles again.
            hParentStdIn = GetStdHandle(STD_INPUT_HANDLE);
            hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
            hParentStdErr = GetStdHandle(STD_ERROR_HANDLE);
        }
    }
}

std::wstring join(const std::vector<std::wstring>& elements, const std::wstring& separator)
{
    if (!elements.empty())
    {
        std::wstringstream ss;
        auto it = elements.cbegin();

        while (true)
        {
            ss << *it++;

            if (it != elements.cend())
                ss << separator;
            else
                return ss.str();
        }
    }

    return L"";
}

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    int count = 0;
    LPWSTR* args;
    std::vector<std::wstring> vec;

    args = CommandLineToArgvW(GetCommandLineW(), &count);

    for (int i = 1; i < count; i++)
    {
        bool hasSpace = wcsstr(args[i], L" ") != NULL;

        if (hasSpace) {
            std::wstring s;
            s.append(L"\"");
            s.append(args[i]);
            s.append(L"\"");
            vec.push_back(s);
        }
        else
            vec.push_back(args[i]);
    }

    auto cmdlstr = join(vec, L" ");
    LPWSTR cmdl = (LPWSTR)cmdlstr.c_str();

    HANDLE hChildStdinRead, hChildStdinWrite;
    HANDLE hChildStdoutRead, hChildStdoutWrite;
    HANDLE hChildStderrRead, hChildStderrWrite;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    CreatePipe(&hChildStdoutRead, &hChildStdoutWrite, &sa, 0);
    CreatePipe(&hChildStderrRead, &hChildStderrWrite, &sa, 0);
    CreatePipe(&hChildStdinRead, &hChildStdinWrite, &sa, 0);

    SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStderrRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStdinWrite, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    si.hStdError = hChildStderrWrite;
    si.hStdOutput = hChildStdoutWrite;
    si.hStdInput = hChildStdinRead;
    si.dwFlags |= STARTF_USESTDHANDLES;
    
    CreateProcess(
        NULL,   // No module name (use command line)
        cmdl,   // Command line
        NULL,   // Process handle not inheritable
        NULL,   // Thread handle not inheritable
        TRUE,  // Set handle inheritance to TRUE
        CREATE_NO_WINDOW, // No creation flags
        NULL,   // Use parent's environment block
        NULL,   // Use parent's starting directory 
        &si,    // Pointer to STARTUPINFO structure
        &pi);   // Pointer to PROCESS_INFORMATION structure

    CloseHandle(hChildStdoutWrite);
    CloseHandle(hChildStderrWrite);
    CloseHandle(hChildStdinRead);

    // --- CHANGED: Conditionally create I/O threads ---

    // NEW: Get parent console handles, attaching if necessary
    HANDLE hParentStdIn, hParentStdOut, hParentStdErr;
    InitializeParentHandles(hParentStdIn, hParentStdOut, hParentStdErr);

    std::vector<HANDLE> hThreads;
    THREAD_PARAMS t_stdin, t_stdout, t_stderr;

    // Only create I/O threads if the corresponding parent handle is valid.
    // If a handle is invalid, we are in a "fire-and-forget" context.
    if (hParentStdIn != NULL && hParentStdIn != INVALID_HANDLE_VALUE) {
        t_stdin.hRead = hParentStdIn;
        t_stdin.hWrite = hChildStdinWrite;
        hThreads.push_back(CreateThread(NULL, 0, IoForwardThread, &t_stdin, 0, NULL));
    } else {
        CloseHandle(hChildStdinWrite); // Pipe is not used, so close it.
    }

    if (hParentStdOut != NULL && hParentStdOut != INVALID_HANDLE_VALUE) {
        t_stdout.hRead = hChildStdoutRead;
        t_stdout.hWrite = hParentStdOut;
        hThreads.push_back(CreateThread(NULL, 0, IoForwardThread, &t_stdout, 0, NULL));
    } else {
        CloseHandle(hChildStdoutRead); // Pipe is not used, so close it.
    }

    if (hParentStdErr != NULL && hParentStdErr != INVALID_HANDLE_VALUE) {
        t_stderr.hRead = hChildStderrRead;
        t_stderr.hWrite = hParentStdErr;
        hThreads.push_back(CreateThread(NULL, 0, IoForwardThread, &t_stderr, 0, NULL));
    } else {
        CloseHandle(hChildStderrRead); // Pipe is not used, so close it.
    }

    // Wait until child process exits.
    WaitForSingleObject(pi.hProcess, INFINITE);
    
    // Wait for any I/O threads that were created to complete.
    if (!hThreads.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(hThreads.size()), hThreads.data(), TRUE, INFINITE);
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    
    // Close process and thread handles. 
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    for (HANDLE hThread : hThreads) {
        CloseHandle(hThread);
    }
    
    // NEW: Detach from the console if we attached to it
    FreeConsole();

    return exit_code;
}

DWORD WINAPI IoForwardThread(LPVOID lpParam) {
    THREAD_PARAMS* params = (THREAD_PARAMS*)lpParam;
    CHAR chBuf[4096];
    DWORD dwRead, dwWritten;

    while (ReadFile(params->hRead, chBuf, 4096, &dwRead, NULL) && dwRead != 0) {
        if (!WriteFile(params->hWrite, chBuf, dwRead, &dwWritten, NULL)) {
            // Stop if we can't write to the destination pipe (e.g., it was closed)
            break; 
        }
    }

    return 0;
}
