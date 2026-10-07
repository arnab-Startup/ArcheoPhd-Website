#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <fstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

// Resource IDs
#define IDR_ARCHAEOPHD_EXE 101
#define IDR_WEBVIEW2_DLL   102

// Control IDs
#define IDC_INSTALL_PATH_EDIT      1001
#define IDC_INSTALL_PATH_BROWSE    1002
#define IDC_MODELS_DRIVE_COMBO     1003
#define IDC_MODELS_PATH_EDIT       1004
#define IDC_MODELS_PATH_BROWSE     1005
#define IDC_PROJECTS_PATH_EDIT     1006
#define IDC_PROJECTS_PATH_BROWSE   1007
#define IDC_DESKTOP_SHORTCUT_CHK   1008
#define IDC_STARTMENU_SHORTCUT_CHK 1009
#define IDC_PROGRESS_BAR           1010
#define IDC_STATUS_LABEL           1011
#define IDC_INSTALL_BTN            1012
#define IDC_CANCEL_BTN             1013

struct DriveInfo {
    std::wstring root;
    double freeGB;
    double totalGB;
};

// Global UI Handles
HWND g_hWnd = NULL;
HWND g_hInstallEdit = NULL;
HWND g_hInstallBrowse = NULL;
HWND g_hModelsDriveCombo = NULL;
HWND g_hModelsEdit = NULL;
HWND g_hModelsBrowse = NULL;
HWND g_hProjectsEdit = NULL;
HWND g_hProjectsBrowse = NULL;
HWND g_hDesktopChk = NULL;
HWND g_hStartMenuChk = NULL;
HWND g_hInstallBtn = NULL;
HWND g_hCancelBtn = NULL;
HWND g_hProgressBar = NULL;
HWND g_hStatusLabel = NULL;

HFONT g_hFontTitle = NULL;
HFONT g_hFontBold = NULL;
HFONT g_hFontNormal = NULL;
HFONT g_hFontSmall = NULL;

std::vector<DriveInfo> g_drives;
std::wstring g_defaultInstallDir;
std::wstring g_defaultModelsDir;
std::wstring g_defaultProjectsDir;
std::atomic<bool> g_isInstalling(false);
std::atomic<bool> g_installComplete(false);

// Utility: Enumerate Fixed Drives and free space
std::vector<DriveInfo> EnumerateFixedDrives() {
    std::vector<DriveInfo> drives;
    DWORD len = GetLogicalDriveStringsW(0, NULL);
    if (len == 0) return drives;

    std::vector<wchar_t> buffer(len + 1);
    GetLogicalDriveStringsW(len, buffer.data());

    wchar_t* drive = buffer.data();
    while (*drive) {
        UINT type = GetDriveTypeW(drive);
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
            ULARGE_INTEGER freeBytes, totalBytes, totalFree;
            if (GetDiskFreeSpaceExW(drive, &freeBytes, &totalBytes, &totalFree)) {
                DriveInfo info;
                info.root = drive;
                info.freeGB = (double)freeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
                info.totalGB = (double)totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
                drives.push_back(info);
            }
        }
        drive += wcslen(drive) + 1;
    }
    return drives;
}

// Utility: Extract Embedded Resource to File
bool ExtractResourceToFile(int resourceId, const std::wstring& outputPath) {
    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return false;
    DWORD size = SizeofResource(NULL, hRes);
    if (size == 0) return false;
    const void* pData = LockResource(hData);
    if (!pData) return false;

    HANDLE hFile = CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(hFile, pData, size, &bytesWritten, NULL);
    CloseHandle(hFile);
    return (ok && bytesWritten == size);
}

// Utility: Create Windows Shortcut (.lnk)
bool CreateShortcut(const std::wstring& targetPath, const std::wstring& shortcutPath, const std::wstring& description, const std::wstring& workingDir) {
    HRESULT hr = CoInitialize(NULL);
    IShellLinkW* psl = NULL;
    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
    if (SUCCEEDED(hr)) {
        psl->SetPath(targetPath.c_str());
        psl->SetDescription(description.c_str());
        psl->SetWorkingDirectory(workingDir.c_str());

        IPersistFile* ppf = NULL;
        hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(shortcutPath.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
    return SUCCEEDED(hr);
}

// Folder Browser Dialog
std::wstring BrowseForFolder(HWND hWnd, const std::wstring& title) {
    wchar_t path[MAX_PATH] = { 0 };
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = hWnd;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != 0) {
        SHGetPathFromIDListW(pidl, path);
        IMalloc* imalloc = 0;
        if (SUCCEEDED(SHGetMalloc(&imalloc))) {
            imalloc->Free(pidl);
            imalloc->Release();
        }
        return std::wstring(path);
    }
    return L"";
}

// Installation Worker Thread
void RunInstallationThread() {
    g_isInstalling = true;

    wchar_t installDirBuf[MAX_PATH] = { 0 };
    wchar_t modelsDirBuf[MAX_PATH] = { 0 };
    wchar_t projectsDirBuf[MAX_PATH] = { 0 };
    GetWindowTextW(g_hInstallEdit, installDirBuf, MAX_PATH);
    GetWindowTextW(g_hModelsEdit, modelsDirBuf, MAX_PATH);
    GetWindowTextW(g_hProjectsEdit, projectsDirBuf, MAX_PATH);

    std::wstring installDir = installDirBuf;
    std::wstring modelsDir = modelsDirBuf;
    std::wstring projectsDir = projectsDirBuf;
    bool createDesktop = (SendMessage(g_hDesktopChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool createStartMenu = (SendMessage(g_hStartMenuChk, BM_GETCHECK, 0, 0) == BST_CHECKED);

    auto setStatus = [](const std::wstring& msg, int progress) {
        SetWindowTextW(g_hStatusLabel, msg.c_str());
        SendMessage(g_hProgressBar, PBM_SETPOS, progress, 0);
    };

    try {
        setStatus(L"Step 1/5: Creating target directories...", 15);
        fs::create_directories(installDir);
        fs::create_directories(modelsDir + L"\\llm");
        fs::create_directories(modelsDir + L"\\embedding");
        fs::create_directories(projectsDir);

        Sleep(250);
        setStatus(L"Step 2/5: Extracting core application to C: drive...", 40);

        std::wstring exeDst = installDir + L"\\ArchaeoPhD.exe";
        bool extractedExe = ExtractResourceToFile(IDR_ARCHAEOPHD_EXE, exeDst);

        // Fallback if running outside embedded resource bundle
        if (!extractedExe) {
            std::vector<std::wstring> fallbackPaths = {
                L"ArchaeoPhD.exe",
                L"..\\desktop\\release\\ArchaeoPhD.exe",
                L"D:\\Prorgram\\Project\\ArcheoPhd\\desktop\\release\\ArchaeoPhD.exe"
            };
            for (const auto& p : fallbackPaths) {
                if (fs::exists(p)) {
                    fs::copy_file(p, exeDst, fs::copy_options::overwrite_existing);
                    extractedExe = true;
                    break;
                }
            }
        }

        Sleep(200);
        setStatus(L"Step 3/5: Installing WebView2 dynamic dependencies...", 60);

        std::wstring dllDst = installDir + L"\\WebView2Loader.dll";
        bool extractedDll = ExtractResourceToFile(IDR_WEBVIEW2_DLL, dllDst);

        if (!extractedDll) {
            std::vector<std::wstring> fallbackDlls = {
                L"WebView2Loader.dll",
                L"..\\desktop\\release\\WebView2Loader.dll",
                L"D:\\Prorgram\\Project\\ArcheoPhd\\desktop\\release\\WebView2Loader.dll"
            };
            for (const auto& p : fallbackDlls) {
                if (fs::exists(p)) {
                    fs::copy_file(p, dllDst, fs::copy_options::overwrite_existing);
                    extractedDll = true;
                    break;
                }
            }
        }

        Sleep(200);
        setStatus(L"Step 4/5: Configuring AI Models & Research Workspace paths...", 80);

        // Helper to escape backslashes for JSON
        auto escapeJsonPath = [](const std::wstring& p) {
            std::wstring res;
            for (wchar_t c : p) {
                if (c == L'\\') res += L"\\\\";
                else res += c;
            }
            return res;
        };

        // Write central configuration
        std::wofstream cfgFile((installDir + L"\\archaeophd.config.json").c_str());
        if (cfgFile.is_open()) {
            cfgFile << L"{\n";
            cfgFile << L"  \"version\": \"1.0.0\",\n";
            cfgFile << L"  \"installPath\": \"" << escapeJsonPath(installDir) << L"\",\n";
            cfgFile << L"  \"modelsPath\": \"" << escapeJsonPath(modelsDir) << L"\",\n";
            cfgFile << L"  \"projectsPath\": \"" << escapeJsonPath(projectsDir) << L"\"\n";
            cfgFile << L"}\n";
            cfgFile.close();
        }

        // Link existing repo models if present on this workstation
        std::wstring repoLlm = L"D:\\Prorgram\\Project\\ArcheoPhd\\desktop\\models\\llm\\Qwen2.5-7B-Instruct-Q4_K_M.gguf";
        std::wstring repoEmb = L"D:\\Prorgram\\Project\\ArcheoPhd\\desktop\\models\\embedding\\nomic-embed-text-v1.5.Q4_K_M.gguf";
        std::wstring targetLlm = modelsDir + L"\\llm\\Qwen2.5-7B-Instruct-Q4_K_M.gguf";
        std::wstring targetEmb = modelsDir + L"\\embedding\\nomic-embed-text-v1.5.Q4_K_M.gguf";

        if (!fs::exists(targetLlm) && fs::exists(repoLlm)) {
            setStatus(L"Linking offline neural reasoning model (Qwen 7B)...", 85);
            try { fs::copy_file(repoLlm, targetLlm, fs::copy_options::overwrite_existing); } catch(...) {}
        }
        if (!fs::exists(targetEmb) && fs::exists(repoEmb)) {
            setStatus(L"Linking offline embedding vectors (Nomic)...", 90);
            try { fs::copy_file(repoEmb, targetEmb, fs::copy_options::overwrite_existing); } catch(...) {}
        }

        setStatus(L"Step 5/5: Creating Windows application shortcuts...", 95);
        if (createDesktop) {
            wchar_t desktopPath[MAX_PATH] = { 0 };
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
                std::wstring link = std::wstring(desktopPath) + L"\\ArchaeoPhD.lnk";
                CreateShortcut(exeDst, link, L"ArchaeoPhD - AI Research Workspace for Archaeology", installDir);
            }
        }

        if (createStartMenu) {
            wchar_t programsPath[MAX_PATH] = { 0 };
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, programsPath))) {
                std::wstring link = std::wstring(programsPath) + L"\\ArchaeoPhD.lnk";
                CreateShortcut(exeDst, link, L"ArchaeoPhD - AI Research Workspace for Archaeology", installDir);
            }
        }

        Sleep(200);
        setStatus(L"Installation complete! ArchaeoPhD is configured and ready.", 100);
        g_installComplete = true;
    }
    catch (const std::exception& ex) {
        SetWindowTextW(g_hStatusLabel, L"Installation encountered an error.");
        g_installComplete = false;
    }

    g_isInstalling = false;

    // Switch UI to Complete state
    EnableWindow(g_hCancelBtn, FALSE);
    SetWindowTextW(g_hInstallBtn, L"Launch ArchaeoPhD");
    EnableWindow(g_hInstallBtn, TRUE);
    SetFocus(g_hInstallBtn);
}

// Window Procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Create Fonts
        g_hFontTitle = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_hFontBold = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_hFontNormal = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_hFontSmall = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        // Enumerate drives to find the one with the most space
        g_drives = EnumerateFixedDrives();
        std::wstring bestRoot = L"C:\\";
        double maxFree = 0.0;
        for (const auto& d : g_drives) {
            if (d.freeGB > maxFree) {
                maxFree = d.freeGB;
                bestRoot = d.root;
            }
        }

        // Tier 1 Default: Core App on C: drive (%LOCALAPPDATA%\ArchaeoPhD)
        wchar_t localApp[MAX_PATH] = { 0 };
        SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localApp);
        g_defaultInstallDir = std::wstring(localApp) + L"\\ArchaeoPhD";

        // Tier 2 Default: AI Models on drive with most free space (e.g. D:\ArchaeoPhD\models)
        g_defaultModelsDir = bestRoot + L"ArchaeoPhD\\models";

        // Tier 3 Default: Research Projects in User's Documents
        wchar_t docsPath[MAX_PATH] = { 0 };
        SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, docsPath);
        g_defaultProjectsDir = std::wstring(docsPath) + L"\\ArchaeoPhD-Projects";

        // Header
        HWND hTitle = CreateWindowW(L"STATIC", L"ArchaeoPhD Workstation Setup", WS_CHILD | WS_VISIBLE, 24, 16, 560, 26, hWnd, NULL, NULL, NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        HWND hSub = CreateWindowW(L"STATIC", L"Customize where the application basics, heavy AI models, and research projects live.", WS_CHILD | WS_VISIBLE, 24, 44, 560, 18, hWnd, NULL, NULL, NULL);
        SendMessage(hSub, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // -------------------------------------------------------------
        // TIER 1: Core Application Basics (C: Drive)
        // -------------------------------------------------------------
        HWND hLbl1 = CreateWindowW(L"STATIC", L"1. Application Basics (~15 MB) \x2014 C: Drive", WS_CHILD | WS_VISIBLE, 24, 72, 450, 18, hWnd, NULL, NULL, NULL);
        SendMessage(hLbl1, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hInstallEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_defaultInstallDir.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 24, 92, 450, 24, hWnd, (HMENU)IDC_INSTALL_PATH_EDIT, NULL, NULL);
        SendMessage(g_hInstallEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hInstallBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 482, 92, 85, 24, hWnd, (HMENU)IDC_INSTALL_PATH_BROWSE, NULL, NULL);
        SendMessage(g_hInstallBrowse, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        HWND hTip1 = CreateWindowW(L"STATIC", L"Standard Windows program directory for C++ engine & WebView2 UI runtime.", WS_CHILD | WS_VISIBLE, 24, 118, 540, 15, hWnd, NULL, NULL, NULL);
        SendMessage(hTip1, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        // -------------------------------------------------------------
        // TIER 2: AI Neural Models Storage (~4.5 GB)
        // -------------------------------------------------------------
        HWND hLbl2 = CreateWindowW(L"STATIC", L"2. AI Models & Neural Weights Storage (~4.5 GB)", WS_CHILD | WS_VISIBLE, 24, 142, 450, 18, hWnd, NULL, NULL, NULL);
        SendMessage(hLbl2, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hDesc2 = CreateWindowW(L"STATIC", L"Select storage drive for 7B Qwen LLM weights and vector embedding index:", WS_CHILD | WS_VISIBLE, 24, 160, 540, 15, hWnd, NULL, NULL, NULL);
        SendMessage(hDesc2, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        g_hModelsDriveCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 24, 178, 450, 160, hWnd, (HMENU)IDC_MODELS_DRIVE_COMBO, NULL, NULL);
        SendMessage(g_hModelsDriveCombo, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        int selectIdx = 0;
        int idx = 0;
        for (const auto& d : g_drives) {
            std::wstringstream ss;
            ss << L"Drive " << d.root << L" (" << std::fixed << std::setprecision(1) << d.freeGB << L" GB Free of " << d.totalGB << L" GB)";
            if (d.root == bestRoot) {
                ss << L"  [RECOMMENDED - Spacious]";
                selectIdx = idx;
            } else if (d.freeGB < 15.0) {
                ss << L"  [Low Space]";
            }
            SendMessage(g_hModelsDriveCombo, CB_ADDSTRING, 0, (LPARAM)ss.str().c_str());
            idx++;
        }
        SendMessage(g_hModelsDriveCombo, CB_SETCURSEL, selectIdx, 0);

        g_hModelsEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_defaultModelsDir.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 24, 206, 450, 24, hWnd, (HMENU)IDC_MODELS_PATH_EDIT, NULL, NULL);
        SendMessage(g_hModelsEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hModelsBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 482, 206, 85, 24, hWnd, (HMENU)IDC_MODELS_PATH_BROWSE, NULL, NULL);
        SendMessage(g_hModelsBrowse, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // -------------------------------------------------------------
        // TIER 3: Research Workspace & Projects
        // -------------------------------------------------------------
        HWND hLbl3 = CreateWindowW(L"STATIC", L"3. Research Workspace & Project Library (Field Reports & Studies)", WS_CHILD | WS_VISIBLE, 24, 238, 540, 18, hWnd, NULL, NULL, NULL);
        SendMessage(hLbl3, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hProjectsEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_defaultProjectsDir.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 24, 258, 450, 24, hWnd, (HMENU)IDC_PROJECTS_PATH_EDIT, NULL, NULL);
        SendMessage(g_hProjectsEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hProjectsBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 482, 258, 85, 24, hWnd, (HMENU)IDC_PROJECTS_PATH_BROWSE, NULL, NULL);
        SendMessage(g_hProjectsBrowse, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        HWND hTip3 = CreateWindowW(L"STATIC", L"Where your Harris matrices, C-14 records, and notes are saved. Safe for cloud sync.", WS_CHILD | WS_VISIBLE, 24, 284, 540, 15, hWnd, NULL, NULL, NULL);
        SendMessage(hTip3, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        // -------------------------------------------------------------
        // Shortcuts
        // -------------------------------------------------------------
        g_hDesktopChk = CreateWindowW(L"BUTTON", L"Create Desktop shortcut", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 308, 220, 20, hWnd, (HMENU)IDC_DESKTOP_SHORTCUT_CHK, NULL, NULL);
        SendMessage(g_hDesktopChk, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        SendMessage(g_hDesktopChk, BM_SETCHECK, BST_CHECKED, 0);

        g_hStartMenuChk = CreateWindowW(L"BUTTON", L"Create Start Menu shortcut", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 260, 308, 220, 20, hWnd, (HMENU)IDC_STARTMENU_SHORTCUT_CHK, NULL, NULL);
        SendMessage(g_hStartMenuChk, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        SendMessage(g_hStartMenuChk, BM_SETCHECK, BST_CHECKED, 0);

        // -------------------------------------------------------------
        // Progress Bar & Status
        // -------------------------------------------------------------
        g_hProgressBar = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 24, 340, 543, 16, hWnd, (HMENU)IDC_PROGRESS_BAR, NULL, NULL);
        SendMessage(g_hProgressBar, PBM_SETRANGE32, 0, 100);
        SendMessage(g_hProgressBar, PBM_SETPOS, 0, 0);

        g_hStatusLabel = CreateWindowW(L"STATIC", L"Ready to install. Click Install to begin.", WS_CHILD | WS_VISIBLE, 24, 362, 543, 18, hWnd, (HMENU)IDC_STATUS_LABEL, NULL, NULL);
        SendMessage(g_hStatusLabel, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // -------------------------------------------------------------
        // Action Buttons
        // -------------------------------------------------------------
        g_hInstallBtn = CreateWindowW(L"BUTTON", L"Install", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 360, 396, 115, 32, hWnd, (HMENU)IDC_INSTALL_BTN, NULL, NULL);
        SendMessage(g_hInstallBtn, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hCancelBtn = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 485, 396, 82, 32, hWnd, (HMENU)IDC_CANCEL_BTN, NULL, NULL);
        SendMessage(g_hCancelBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_INSTALL_PATH_BROWSE) {
            std::wstring folder = BrowseForFolder(hWnd, L"Select Application Installation Directory (C: Drive)");
            if (!folder.empty()) {
                SetWindowTextW(g_hInstallEdit, folder.c_str());
            }
        }
        else if (id == IDC_MODELS_PATH_BROWSE) {
            std::wstring folder = BrowseForFolder(hWnd, L"Select AI Models & Neural Weights Storage Directory");
            if (!folder.empty()) {
                SetWindowTextW(g_hModelsEdit, folder.c_str());
            }
        }
        else if (id == IDC_PROJECTS_PATH_BROWSE) {
            std::wstring folder = BrowseForFolder(hWnd, L"Select Research Workspace & Projects Directory");
            if (!folder.empty()) {
                SetWindowTextW(g_hProjectsEdit, folder.c_str());
            }
        }
        else if (id == IDC_MODELS_DRIVE_COMBO && HIWORD(wParam) == CBN_SELCHANGE) {
            int sel = SendMessage(g_hModelsDriveCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_drives.size()) {
                std::wstring newPath = g_drives[sel].root + L"ArchaeoPhD\\models";
                SetWindowTextW(g_hModelsEdit, newPath.c_str());
            }
        }
        else if (id == IDC_INSTALL_BTN) {
            if (g_installComplete) {
                // Launch clicked
                wchar_t installDirBuf[MAX_PATH] = { 0 };
                GetWindowTextW(g_hInstallEdit, installDirBuf, MAX_PATH);
                std::wstring exePath = std::wstring(installDirBuf) + L"\\ArchaeoPhD.exe";
                ShellExecuteW(NULL, L"open", exePath.c_str(), NULL, installDirBuf, SW_SHOWNORMAL);
                PostQuitMessage(0);
            }
            else if (!g_isInstalling) {
                // Disable controls during installation
                EnableWindow(g_hInstallEdit, FALSE);
                EnableWindow(g_hInstallBrowse, FALSE);
                EnableWindow(g_hModelsDriveCombo, FALSE);
                EnableWindow(g_hModelsEdit, FALSE);
                EnableWindow(g_hModelsBrowse, FALSE);
                EnableWindow(g_hProjectsEdit, FALSE);
                EnableWindow(g_hProjectsBrowse, FALSE);
                EnableWindow(g_hDesktopChk, FALSE);
                EnableWindow(g_hStartMenuChk, FALSE);
                EnableWindow(g_hInstallBtn, FALSE);
                EnableWindow(g_hCancelBtn, FALSE);
                std::thread(RunInstallationThread).detach();
            }
        }
        else if (id == IDC_CANCEL_BTN) {
            if (!g_isInstalling) {
                PostQuitMessage(0);
            }
        }
        break;
    }

    case WM_CLOSE: {
        if (!g_isInstalling) {
            DestroyWindow(hWnd);
        }
        return 0;
    }

    case WM_DESTROY: {
        if (g_hFontTitle) DeleteObject(g_hFontTitle);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hFontNormal) DeleteObject(g_hFontNormal);
        if (g_hFontSmall) DeleteObject(g_hFontSmall);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    InitCommonControls();
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_USEREX_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ArchaeoPhDSetupWizardClass";
    RegisterClassExW(&wc);

    // Center window on screen
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 615;
    int winH = 485;
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 2;

    g_hWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"ArchaeoPhDSetupWizardClass",
        L"ArchaeoPhD Desktop Workstation — Setup Wizard",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        winX, winY, winW, winH,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWnd) return 1;

    ShowWindow(g_hWnd, SW_SHOWNORMAL);
    UpdateWindow(g_hWnd);
    SetForegroundWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
