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
#define IDC_INSTALL_PATH_EDIT     1001
#define IDC_INSTALL_PATH_BROWSE   1002
#define IDC_DATA_PATH_EDIT        1003
#define IDC_DATA_PATH_BROWSE      1004
#define IDC_DRIVE_COMBO           1005
#define IDC_DESKTOP_SHORTCUT_CHK  1006
#define IDC_STARTMENU_SHORTCUT_CHK 1007
#define IDC_INSTALL_BTN           1009
#define IDC_CANCEL_BTN            1010
#define IDC_PROGRESS_BAR          1011
#define IDC_STATUS_LABEL          1012

struct DriveInfo {
    std::wstring root;
    double freeGB;
    double totalGB;
};

// Global UI Handles
HWND g_hWnd = NULL;
HWND g_hInstallEdit = NULL;
HWND g_hInstallBrowse = NULL;
HWND g_hDataEdit = NULL;
HWND g_hDataBrowse = NULL;
HWND g_hDriveCombo = NULL;
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
std::wstring g_defaultDataRoot;
std::wstring g_defaultInstallDir;
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
    wchar_t dataDirBuf[MAX_PATH] = { 0 };
    GetWindowTextW(g_hInstallEdit, installDirBuf, MAX_PATH);
    GetWindowTextW(g_hDataEdit, dataDirBuf, MAX_PATH);

    std::wstring installDir = installDirBuf;
    std::wstring dataDir = dataDirBuf;
    bool createDesktop = (SendMessage(g_hDesktopChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool createStartMenu = (SendMessage(g_hStartMenuChk, BM_GETCHECK, 0, 0) == BST_CHECKED);

    auto setStatus = [](const std::wstring& msg, int progress) {
        SetWindowTextW(g_hStatusLabel, msg.c_str());
        SendMessage(g_hProgressBar, PBM_SETPOS, progress, 0);
    };

    try {
        setStatus(L"Creating installation directories...", 15);
        fs::create_directories(installDir);
        fs::create_directories(dataDir + L"\\models\\llm");
        fs::create_directories(dataDir + L"\\models\\embedding");
        fs::create_directories(dataDir + L"\\projects");

        Sleep(250);
        setStatus(L"Extracting ArchaeoPhD native workstation engine...", 40);

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
        setStatus(L"Extracting WebView2 dynamic runtime loader...", 65);

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
        setStatus(L"Configuring Research Data Root & local model storage...", 80);

        // Write config file into install directory and data root
        std::wofstream cfgFile((installDir + L"\\archaeophd.config.json").c_str());
        if (cfgFile.is_open()) {
            cfgFile << L"{\n";
            cfgFile << L"  \"version\": \"1.0.0\",\n";
            cfgFile << L"  \"dataPath\": \"" << dataDir << L"\",\n";
            cfgFile << L"  \"modelsDir\": \"" << dataDir << L"\\\\models\",\n";
            cfgFile << L"  \"projectsDir\": \"" << dataDir << L"\\\\projects\"\n";
            cfgFile << L"}\n";
            cfgFile.close();
        }

        // Link existing repo models if present on this workstation
        std::wstring repoLlm = L"D:\\Prorgram\\Project\\ArcheoPhd\\desktop\\models\\llm\\Qwen2.5-7B-Instruct-Q4_K_M.gguf";
        std::wstring repoEmb = L"D:\\Prorgram\\Project\\ArcheoPhd\\desktop\\models\\embedding\\nomic-embed-text-v1.5.Q4_K_M.gguf";
        std::wstring targetLlm = dataDir + L"\\models\\llm\\Qwen2.5-7B-Instruct-Q4_K_M.gguf";
        std::wstring targetEmb = dataDir + L"\\models\\embedding\\nomic-embed-text-v1.5.Q4_K_M.gguf";

        if (!fs::exists(targetLlm) && fs::exists(repoLlm)) {
            setStatus(L"Linking offline neural reasoning model (Qwen 7B)...", 85);
            try { fs::copy_file(repoLlm, targetLlm, fs::copy_options::overwrite_existing); } catch(...) {}
        }
        if (!fs::exists(targetEmb) && fs::exists(repoEmb)) {
            setStatus(L"Linking offline embedding vectors (Nomic)...", 90);
            try { fs::copy_file(repoEmb, targetEmb, fs::copy_options::overwrite_existing); } catch(...) {}
        }

        setStatus(L"Creating Windows application shortcuts...", 95);
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
        setStatus(L"Installation complete! ArchaeoPhD is ready to launch.", 100);
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
        g_hFontBold = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_hFontNormal = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
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

        // Set Default Paths
        wchar_t localApp[MAX_PATH] = { 0 };
        SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localApp);
        g_defaultInstallDir = std::wstring(localApp) + L"\\ArchaeoPhD";
        g_defaultDataRoot = bestRoot + L"ArchaeoPhD-Data";

        // Title & Header
        HWND hTitle = CreateWindowW(L"STATIC", L"ArchaeoPhD Desktop Workstation Setup", WS_CHILD | WS_VISIBLE, 24, 18, 540, 28, hWnd, NULL, NULL, NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        HWND hSub = CreateWindowW(L"STATIC", L"Evidence-Traceable AI Research Workspace for Archaeology", WS_CHILD | WS_VISIBLE, 24, 48, 540, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hSub, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Section 1: Application Files
        HWND hLbl1 = CreateWindowW(L"STATIC", L"1. Application Installation Directory (~15 MB):", WS_CHILD | WS_VISIBLE, 24, 82, 350, 18, hWnd, NULL, NULL, NULL);
        SendMessage(hLbl1, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hInstallEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_defaultInstallDir.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 24, 104, 430, 26, hWnd, (HMENU)IDC_INSTALL_PATH_EDIT, NULL, NULL);
        SendMessage(g_hInstallEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hInstallBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 464, 104, 90, 26, hWnd, (HMENU)IDC_INSTALL_PATH_BROWSE, NULL, NULL);
        SendMessage(g_hInstallBrowse, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        HWND hTip1 = CreateWindowW(L"STATIC", L"Installs native C++ engine, UI workstation, and WebView2 runtime loader.", WS_CHILD | WS_VISIBLE, 24, 134, 530, 16, hWnd, NULL, NULL, NULL);
        SendMessage(hTip1, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        // Section 2: Research Data & Models Root (4.8 GB)
        HWND hLbl2 = CreateWindowW(L"STATIC", L"2. Research Data & Offline AI Models Root (~4.8 GB):", WS_CHILD | WS_VISIBLE, 24, 160, 480, 18, hWnd, NULL, NULL, NULL);
        SendMessage(hLbl2, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hDesc2 = CreateWindowW(L"STATIC", L"Choose storage drive for 7B neural weights, embeddings, and research projects:", WS_CHILD | WS_VISIBLE, 24, 180, 530, 16, hWnd, NULL, NULL, NULL);
        SendMessage(hDesc2, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        g_hDriveCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 24, 202, 430, 160, hWnd, (HMENU)IDC_DRIVE_COMBO, NULL, NULL);
        SendMessage(g_hDriveCombo, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Populate Drives Combo
        int selectIdx = 0;
        int idx = 0;
        for (const auto& d : g_drives) {
            std::wstringstream ss;
            ss << L"Drive " << d.root << L" (" << std::fixed << std::setprecision(1) << d.freeGB << L" GB Free of " << d.totalGB << L" GB)";
            if (d.root == bestRoot) {
                ss << L"  [RECOMMENDED - Spacious]";
                selectIdx = idx;
            } else if (d.freeGB < 15.0) {
                ss << L"  [Low Free Space]";
            }
            SendMessage(g_hDriveCombo, CB_ADDSTRING, 0, (LPARAM)ss.str().c_str());
            idx++;
        }
        SendMessage(g_hDriveCombo, CB_SETCURSEL, selectIdx, 0);

        g_hDataEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_defaultDataRoot.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 24, 236, 430, 26, hWnd, (HMENU)IDC_DATA_PATH_EDIT, NULL, NULL);
        SendMessage(g_hDataEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hDataBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 464, 236, 90, 26, hWnd, (HMENU)IDC_DATA_PATH_BROWSE, NULL, NULL);
        SendMessage(g_hDataBrowse, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Checkboxes
        g_hDesktopChk = CreateWindowW(L"BUTTON", L"Create Desktop shortcut", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 274, 250, 20, hWnd, (HMENU)IDC_DESKTOP_SHORTCUT_CHK, NULL, NULL);
        SendMessage(g_hDesktopChk, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        SendMessage(g_hDesktopChk, BM_SETCHECK, BST_CHECKED, 0);

        g_hStartMenuChk = CreateWindowW(L"BUTTON", L"Create Start Menu shortcut", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 280, 274, 250, 20, hWnd, (HMENU)IDC_STARTMENU_SHORTCUT_CHK, NULL, NULL);
        SendMessage(g_hStartMenuChk, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        SendMessage(g_hStartMenuChk, BM_SETCHECK, BST_CHECKED, 0);

        // Progress Bar & Status
        g_hProgressBar = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 24, 312, 530, 18, hWnd, (HMENU)IDC_PROGRESS_BAR, NULL, NULL);
        SendMessage(g_hProgressBar, PBM_SETRANGE32, 0, 100);
        SendMessage(g_hProgressBar, PBM_SETPOS, 0, 0);

        g_hStatusLabel = CreateWindowW(L"STATIC", L"Ready to install. Click Install to begin.", WS_CHILD | WS_VISIBLE, 24, 336, 530, 20, hWnd, (HMENU)IDC_STATUS_LABEL, NULL, NULL);
        SendMessage(g_hStatusLabel, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Action Buttons
        g_hInstallBtn = CreateWindowW(L"BUTTON", L"Install", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 360, 375, 104, 32, hWnd, (HMENU)IDC_INSTALL_BTN, NULL, NULL);
        SendMessage(g_hInstallBtn, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hCancelBtn = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 474, 375, 80, 32, hWnd, (HMENU)IDC_CANCEL_BTN, NULL, NULL);
        SendMessage(g_hCancelBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_INSTALL_PATH_BROWSE) {
            std::wstring folder = BrowseForFolder(hWnd, L"Select Program Installation Directory");
            if (!folder.empty()) {
                SetWindowTextW(g_hInstallEdit, folder.c_str());
            }
        }
        else if (id == IDC_DATA_PATH_BROWSE) {
            std::wstring folder = BrowseForFolder(hWnd, L"Select Research Data & Models Root Directory");
            if (!folder.empty()) {
                SetWindowTextW(g_hDataEdit, folder.c_str());
            }
        }
        else if (id == IDC_DRIVE_COMBO && HIWORD(wParam) == CBN_SELCHANGE) {
            int sel = SendMessage(g_hDriveCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_drives.size()) {
                std::wstring newPath = g_drives[sel].root + L"ArchaeoPhD-Data";
                SetWindowTextW(g_hDataEdit, newPath.c_str());
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
                EnableWindow(g_hDataEdit, FALSE);
                EnableWindow(g_hDataBrowse, FALSE);
                EnableWindow(g_hDriveCombo, FALSE);
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
    int winW = 600;
    int winH = 465;
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
