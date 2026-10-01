// Minimal x86 DLL injector.
//
// Built as 32-bit on purpose: the target (SporeApp.exe) is 32-bit, and a
// same-bitness injector can reuse its own kernel32 base, because ASLR picks
// one base per boot and shares it across processes.
//
//   inject.exe <process.exe> <full\path\to.dll>

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>

static DWORD FindPid(const char* exeName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, exeName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        printf("usage: inject.exe <process.exe> <full path to dll>\n");
        return 2;
    }

    const char* target = argv[1];
    const char* dll    = argv[2];

    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) {
        printf("[!] dll not found: %s\n", dll);
        return 1;
    }

    DWORD pid = FindPid(target);
    if (!pid) {
        printf("[!] process not running: %s\n", target);
        return 1;
    }
    printf("[+] %s pid=%lu\n", target, pid);

    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                              PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
                              FALSE, pid);
    if (!proc) {
        printf("[!] OpenProcess failed, err=%lu\n", GetLastError());
        return 1;
    }

    SIZE_T len = strlen(dll) + 1;
    void* remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        printf("[!] VirtualAllocEx failed, err=%lu\n", GetLastError());
        CloseHandle(proc);
        return 1;
    }

    if (!WriteProcessMemory(proc, remote, dll, len, nullptr)) {
        printf("[!] WriteProcessMemory failed, err=%lu\n", GetLastError());
        CloseHandle(proc);
        return 1;
    }

    FARPROC loadLib = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    HANDLE thread = CreateRemoteThread(proc, nullptr, 0,
                                       (LPTHREAD_START_ROUTINE)loadLib, remote, 0, nullptr);
    if (!thread) {
        printf("[!] CreateRemoteThread failed, err=%lu\n", GetLastError());
        CloseHandle(proc);
        return 1;
    }

    WaitForSingleObject(thread, 10000);
    DWORD ret = 0;
    GetExitCodeThread(thread, &ret);
    printf("[+] LoadLibraryA returned 0x%08lX %s\n", ret,
           ret ? "(module loaded)" : "(FAILED - dll refused to load)");

    CloseHandle(thread);
    CloseHandle(proc);
    return ret ? 0 : 1;
}
