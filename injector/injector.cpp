// Usage: injector.exe [path to BedrockClient.dll] [target exe name]
// Default target: Minecraft.Windows.exe
#include <windows.h>
#include <tlhelp32.h>
#include <aclapi.h>
#include <sddl.h>
#include <iostream>
#include <string>
#include <filesystem>

static DWORD findPid(const wchar_t* name) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e{ sizeof(e) };
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(s, &e); ok; ok = Process32NextW(s, &e))
        if (!_wcsicmp(e.szExeFile, name)) { pid = e.th32ProcessID; break; }
    CloseHandle(s);
    return pid;
}

// UWP apps need read access for "ALL APPLICATION PACKAGES" on the DLL
static bool grantAppPackages(const std::wstring& path) {
    PACL oldAcl = nullptr, newAcl = nullptr; PSECURITY_DESCRIPTOR sd = nullptr; PSID sid = nullptr;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl, nullptr, &sd) != ERROR_SUCCESS) return false;
    ConvertStringSidToSidW(L"S-1-15-2-1", &sid);
    EXPLICIT_ACCESSW ea{};
    ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    ea.grfAccessMode = SET_ACCESS; ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID; ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea.Trustee.ptstrName = (LPWSTR)sid;
    bool ok = SetEntriesInAclW(1, &ea, oldAcl, &newAcl) == ERROR_SUCCESS &&
              SetNamedSecurityInfoW((LPWSTR)path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, newAcl, nullptr) == ERROR_SUCCESS;
    if (newAcl) LocalFree(newAcl);
    if (sid) LocalFree(sid);
    if (sd) LocalFree(sd);
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path dll = argc > 1 ? argv[1] : L"BedrockClient.dll";
    const wchar_t* target = argc > 2 ? argv[2] : L"Minecraft.Windows.exe";
    dll = std::filesystem::absolute(dll);
    if (!std::filesystem::exists(dll)) { std::wcout << L"DLL not found: " << dll << L"\n"; return 1; }
    if (!grantAppPackages(dll.wstring())) { std::wcout << L"Failed to set DLL permissions\n"; return 1; }

    std::wcout << L"Waiting for " << target << L" ...\n";
    DWORD pid; while (!(pid = findPid(target))) Sleep(500);
    Sleep(3000);

    HANDLE p = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!p) { std::wcout << L"OpenProcess failed (try running as administrator)\n"; return 1; }
    size_t bytes = (dll.wstring().size() + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(p, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(p, mem, dll.c_str(), bytes, nullptr);
    HANDLE t = CreateRemoteThread(p, nullptr, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"), mem, 0, nullptr);
    if (!t) { std::wcout << L"Injection failed\n"; return 1; }
    WaitForSingleObject(t, 5000);
    VirtualFreeEx(p, mem, 0, MEM_RELEASE);
    CloseHandle(t); CloseHandle(p);
    std::wcout << L"Done. INSERT = menu, END = unload.\n";
}
