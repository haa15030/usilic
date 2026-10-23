// platform.hppのWindowsのネイティブだけのもの(.steering/20261015-windows-native-gui/)。
#include "platform.hpp"

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>

namespace fm {

namespace {

std::wstring widen(const std::string &text) {
  if (text.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
  return out;
}

std::string narrow(const std::wstring &text) {
  if (text.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::string forwardSlashes(std::string path) {
  for (char &c : path)
    if (c == '\\') c = '/';
  return path;
}

}  // namespace

std::string windowsTarCommand() {
  const wchar_t *root = _wgetenv(L"SystemRoot");
  return (root && *root ? narrow(root) : std::string("C:\\Windows")) + "\\System32\\tar.exe";
}

std::vector<std::string> windowsTarArgv(std::initializer_list<std::string> args) {
  std::vector<std::string> argv = {windowsTarCommand(), "--options", "tar:hdrcharset=UTF-8"};
  argv.insert(argv.end(), args.begin(), args.end());
  return argv;
}

std::string windowsSystemTextToUtf8(const std::string &text) {
  if (text.empty()) return text;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0)
    return text;
  // GetACP()はマニフェストのためにUTF-8を返すので、システムの既定のANSIのコードページを尋ねる。
  UINT codePage = CP_ACP;
  DWORD value = 0;
  if (GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                     reinterpret_cast<LPWSTR>(&value), sizeof(value) / sizeof(wchar_t)) > 0 && value)
    codePage = value;
  int n = MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (n <= 0) return text;
  std::wstring wide(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), wide.data(), n);
  return narrow(wide);
}

std::string windowsExecutablePath() {
  std::wstring buf(32768, L'\0');
  DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
  if (n == 0 || n >= buf.size()) return "";
  buf.resize(n);
  return forwardSlashes(narrow(buf));
}

std::string windowsTempDirectory() {
  wchar_t buf[MAX_PATH + 1];
  DWORD n = GetTempPathW(MAX_PATH + 1, buf);
  std::string path = n ? forwardSlashes(narrow(std::wstring(buf, n))) : "C:/Windows/Temp/";
  while (path.size() > 3 && path.back() == '/') path.pop_back();
  return path;
}

void prepareWindowsEnvironment() {
  // 環境変数はUTF-16で読む(狭い文字のgetenvはactiveCodePageがUTF-8でも、起動時の環境の写しによっては化けるため)。
  if (!_wgetenv(L"HOME")) {
    if (const wchar_t *profile = _wgetenv(L"USERPROFILE"))
      _wputenv_s(L"HOME", widen(forwardSlashes(narrow(profile))).c_str());
  }
  if (!_wgetenv(L"USER")) {
    if (const wchar_t *user = _wgetenv(L"USERNAME")) _wputenv_s(L"USER", user);
  }
}

bool replaceFile(const std::string &from, const std::string &to) {
  return MoveFileExW(widen(from).c_str(), widen(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
}

bool openWithWindowsShell(const std::string &target) {
  std::wstring wide = widen(target);
  // URLでなければ\区切りにする(エクスプローラは/のパスを開けないことがある)。
  if (target.find("://") == std::string::npos)
    for (wchar_t &c : wide)
      if (c == L'/') c = L'\\';
  auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return result > 32;
}

void showWindowsError(const std::string &message) {
  MessageBoxW(nullptr, widen(message).c_str(), L"usilic", MB_OK | MB_ICONERROR);
}

// HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersionのProductName(「Windows 10 Pro」)とDisplayVersion(「25H2」)。
// Windows 11でもProductNameは「Windows 10 …」のままなので、ビルド番号22000以上なら11に直す。
std::string osDisplayName() {
  auto read = [](const wchar_t *name) {
    wchar_t buffer[256];
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", name, RRF_RT_REG_SZ,
                     nullptr, buffer, &size) != ERROR_SUCCESS)
      return std::string();
    return narrow(buffer);
  };
  std::string product = read(L"ProductName"), display = read(L"DisplayVersion"), build = read(L"CurrentBuildNumber");
  if (product.empty()) return "Windows";
  if (std::atoi(build.c_str()) >= 22000 && product.compare(0, 10, "Windows 10") == 0) product.replace(8, 2, "11");
  return display.empty() ? product : product + " " + display;
}

}  // namespace fm
