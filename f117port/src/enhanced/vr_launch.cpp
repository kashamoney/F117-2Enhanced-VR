/* ENH: standalone Windows VR startup; no scripts or build tools at runtime. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <cstdio>
/* The upstream C header uses C11's spelling for two runtime declarations. */
#define _Noreturn [[noreturn]]
extern "C" {
#include "platform/platform.h"
}
#undef _Noreturn
#include "enhanced/vr_launch.h"

namespace {
std::string selected;

std::wstring wide(const std::string &text) {
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
    if (!n) return {};
    std::vector<wchar_t> out(n);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, out.data(), n);
    return out.data();
}

std::string utf8(const wchar_t *text) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (!n) return {};
    std::vector<char> out(n);
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
    return out.data();
}

std::string absolute(const std::string &path) {
    auto input = wide(path);
    std::vector<wchar_t> out(32768);
    DWORD n = GetFullPathNameW(input.c_str(), DWORD(out.size()), out.data(), nullptr);
    return n && n < out.size() ? utf8(out.data()) : std::string();
}

std::string find_file(const std::string &directory, const char *name) {
    std::string direct = directory + "/" + name;
    SDL_PathInfo info{};
    if (SDL_GetPathInfo(direct.c_str(), &info) && info.type == SDL_PATHTYPE_FILE) return direct;
    int count = 0;
    char **entries = SDL_GlobDirectory(directory.c_str(), nullptr, 0, &count);
    std::string found;
    for (int i = 0; entries && i < count; ++i)
        if (!SDL_strcasecmp(entries[i], name)) { found = directory + "/" + entries[i]; break; }
    SDL_free(entries);
    return found;
}

std::string problem(const std::string &directory) {
    for (const char *name : {"F117.COM", "MGRAPHIC.EXE", "MISC.EXE"})
        if (find_file(directory, name).empty()) return std::string("Missing ") + name + ". Select the complete original game folder.";
    for (int id = 0; id < PROG_COUNT; ++id) {
        auto file = find_file(directory, prog_table[id].exe);
        if (file.empty()) return std::string("Missing ") + prog_table[id].exe + ". Select the complete original game folder.";
        size_t size = 0;
        auto *data = static_cast<u8 *>(SDL_LoadFile(file.c_str(), &size));
        ExeFile image{};
        const bool parsed = data && exe_parse(data, size, &image);
        SDL_free(data);
        if (!parsed) return std::string(prog_table[id].exe) + " is not a readable original DOS program.";
        char message[768]{};
        const bool supported = exe_build_check(static_cast<ProgId>(id), &image, message, sizeof message);
        exe_free(&image);
        if (!supported) return message;
    }
    return {};
}

bool make_settings_folder(const std::wstring &path) {
    const auto slash = path.find_last_of(L"/\\");
    if (slash != std::wstring::npos) {
        const int rc = SHCreateDirectoryExW(nullptr, path.substr(0, slash).c_str(), nullptr);
        if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) return false;
    }
    return true;
}

bool migrate_settings(const std::string &settings, const std::string &legacy) {
    const auto destination = wide(settings), source = wide(legacy);
    /* Existing new settings always win; retain the legacy file for rollback. */
    if (GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    if (!make_settings_folder(destination)) return false;
    if (CopyFileW(source.c_str(), destination.c_str(), TRUE)) return true;
    const DWORD error = GetLastError();
    return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
}

bool remember(const std::string &settings, const std::string &directory) {
    const auto path = wide(settings);
    if (!make_settings_folder(path)) return false;
    /* A UTF-16 BOM makes the Windows profile API retain non-ASCII folder names. */
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const unsigned char bom[] = {0xFF, 0xFE}; DWORD written = 0;
        const bool ok = WriteFile(file, bom, sizeof bom, &written, nullptr) && written == sizeof bom;
        CloseHandle(file);
        if (!ok) return false;
    } else if (GetLastError() != ERROR_FILE_EXISTS) return false;
    return WritePrivateProfileStringW(L"VR", L"GameDirectory", wide(directory).c_str(), path.c_str()) != FALSE;
}

int browse(std::string &directory) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return -1;
    BROWSEINFOW options{};
    options.lpszTitle = L"F-117A Enhanced VR: select your original game folder (F117.COM / VGAME.EXE).";
    options.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI | BIF_NONEWFOLDERBUTTON;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&options);
    std::vector<wchar_t> path(32768);
    int result = 0;
    if (item) {
        result = SHGetPathFromIDListEx(item, path.data(), DWORD(path.size()), 0) ? 1 : -1;
        if (result == 1) directory = utf8(path.data());
        CoTaskMemFree(item);
    }
    CoUninitialize();
    return result;
}

int fail(const std::string &message, bool interactive) {
    std::fprintf(stderr, "F-117A Enhanced VR: %s\n", message.c_str());
    if (interactive) MessageBoxW(nullptr, wide(message).c_str(), L"F-117A Enhanced VR", MB_OK | MB_ICONERROR);
    return -1;
}
}

int vr_launch_prepare(const char **directory, bool explicit_directory,
                      const char *settings_file, bool interactive, bool choose_again) {
    if (interactive && !settings_file && !SDL_getenv("F117_VR_CONFIG") && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) choose_again = true;
    if (choose_again && !interactive) return fail("--vr-setup needs an interactive desktop. Use --game-dir DIR for terminal setup.", false);
    if (!settings_file) {
        const char *override_file = SDL_getenv("F117_VR_CONFIG");
        if (override_file && *override_file) settings_file = override_file;
    }
    std::string settings;
    if (settings_file) settings = absolute(settings_file);
    else {
        /* Windows APPDATA also permits isolated profiles for automated checks. */
        std::vector<wchar_t> roaming(32768);
        DWORD n = GetEnvironmentVariableW(L"APPDATA", roaming.data(), DWORD(roaming.size()));
        if (!n || n >= roaming.size()) {
            if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, roaming.data())))
                return fail("Cannot open your user settings folder.", interactive);
        }
        const auto root = utf8(roaming.data());
        settings = absolute(root + "/F117-Enhanced-VR/settings.ini");
        const auto legacy = absolute(root + "/kashamoney/F117-Enhanced-VR/settings.ini");
        if (!migrate_settings(settings, legacy))
            return fail("Cannot migrate your saved VR settings. Check that the user settings folder is writable.", interactive);
    }
    if (settings.empty()) return fail("Invalid --vr-config path.", interactive);
    bool supplied = explicit_directory;
    if (explicit_directory) selected = absolute(*directory);
    else if (const char *env = SDL_getenv("F117_GAME_DIR"); env && *env) { selected = absolute(env); supplied = true; }
    else {
        std::vector<wchar_t> path(32768);
        GetPrivateProfileStringW(L"VR", L"GameDirectory", L"", path.data(), DWORD(path.size()), wide(settings).c_str());
        selected = utf8(path.data());
        if (selected.empty()) {
            const char *base = SDL_GetBasePath();
            if (base) selected = std::string(base) + "Game";
        }
    }
    for (;;) {
        std::string issue = selected.empty() ? "Select your original game folder." : problem(selected);
        if (!choose_again && issue.empty()) {
            if (!remember(settings, selected)) return fail("Cannot save your game-folder choice. Check the settings folder is writable.", interactive);
            *directory = selected.c_str();
            /* Explorer and shortcuts may supply unrelated working directories. */
            const char *base = SDL_GetBasePath();
            if (base && !SetCurrentDirectoryW(wide(base).c_str()))
                return fail("Cannot open the extracted application folder.", interactive);
            std::printf("VR game folder: %s\n", selected.c_str());
            return 1;
        }
        if (!interactive) return fail(issue + " Use --game-dir DIR with --vr-config FILE to configure it.", false);
        if (supplied && !choose_again) return fail(issue, true);
        if (!choose_again && !selected.empty() && !find_file(selected, "VGAME.EXE").empty())
            MessageBoxW(nullptr, wide(issue).c_str(), L"F-117A Enhanced VR - game folder", MB_OK | MB_ICONINFORMATION);
        const int result = browse(selected);
        if (result == 0) return 0;
        if (result < 0) return fail("Windows could not open the game-folder picker.", true);
        choose_again = false;
        supplied = false;
    }
}
