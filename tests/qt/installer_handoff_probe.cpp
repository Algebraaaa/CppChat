// Native test executable: record updater arguments, never install or alter a real app.
#ifdef _WIN32
#include <cwchar>
#include <windows.h>

#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
  int count = 0;
  LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!arguments)
    return 1;
  wchar_t path[32768] = {};
  for (int index = 1; index < count; ++index) {
    if (std::wcsncmp(arguments[index], L"/DIR=", 5) == 0) {
      if (std::wcslen(arguments[index] + 5) + 14 >= 32768) {
        LocalFree(arguments);
        return 1;
      }
      std::wcscpy(path, arguments[index] + 5);
      std::wcscat(path, L"\\handoff.txt");
    }
  }
  LocalFree(arguments);
  if (!path[0])
    return 2;
  const HANDLE file =
      CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return 3;
  const wchar_t *command = GetCommandLineW();
  DWORD written = 0;
  const DWORD bytes = static_cast<DWORD>(std::wcslen(command) * sizeof(wchar_t));
  const bool ok = WriteFile(file, command, bytes, &written, nullptr) && written == bytes;
  CloseHandle(file);
  return ok ? 0 : 4;
}
#endif
