
#include <windows.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

BOOL inline static IsValidHandle(HANDLE hHandle) {
    return (hHandle != NULL) && (hHandle != INVALID_HANDLE_VALUE);
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

    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    // Create standard handles for child process
    HANDLE me = GetCurrentProcess();
    
    HANDLE hInput  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hInputDup = NULL;
    if (IsValidHandle(hInput) && DuplicateHandle(me, hInput, me, &hInputDup, 0, TRUE, DUPLICATE_SAME_ACCESS))
    {
        si.hStdInput = hInputDup;
        si.dwFlags |= STARTF_USESTDHANDLES;
    }
    
    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hOutputDup = NULL;
    if (IsValidHandle(hOutput) && DuplicateHandle(me, hOutput, me, &hOutputDup, 0, TRUE, DUPLICATE_SAME_ACCESS))
    {
        si.hStdOutput = hOutputDup;
        si.dwFlags |= STARTF_USESTDHANDLES;
    }
    
    HANDLE hError  = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE hErrorDup = NULL;
    if (IsValidHandle(hError) && DuplicateHandle(me, hError, me, &hErrorDup, 0, TRUE, DUPLICATE_SAME_ACCESS))
    {
        si.hStdError = hErrorDup;
        si.dwFlags |= STARTF_USESTDHANDLES;
    }

    CreateProcess(
        NULL,   // No module name (use command line)
        cmdl,   // Command line
        NULL,   // Process handle not inheritable
        NULL,   // Thread handle not inheritable
        ((si.dwFlags & STARTF_USESTDHANDLES) > 0), // Inherit handles if at least one is valid
        CREATE_NO_WINDOW, // No creation flags
        NULL,   // Use parent's environment block
        NULL,   // Use parent's starting directory 
        &si,    // Pointer to STARTUPINFO structure
        &pi);   // Pointer to PROCESS_INFORMATION structure

    // Wait until child process exits.
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    
    // Close process and thread handles. 
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    // Close duplicate handles
    if (IsValidHandle(hInputDup))   CloseHandle(hInputDup);
    if (IsValidHandle(hOutputDup))  CloseHandle(hOutputDup);
    if (IsValidHandle(hErrorDup))   CloseHandle(hErrorDup);

    return exit_code;
}
