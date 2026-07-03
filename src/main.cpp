// OVRK Workout Reminder
// An always-on-top overlay that automatically detects the end of an OVERKILL
// match (Roblox first-to-5) and pops up a random workout to do before the next
// game.
//
// Detection is fully automatic: it watches the whole screen for OVERKILL's
// match-end signature — the red "Leave" button sitting next to the purple
// "Rematch" button at the bottom-centre — no calibration needed.
//
// Pure Win32 + GDI. No dependencies. Compiles to a single .exe.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cwchar>
#include <ctime>

// ------------------------------------------------------------------ constants
static const int GRID_W = 320;          // downscaled screen used for detection
static const int GRID_H = 180;
static const int REQUIRED_STREAK = 2;   // positive polls (leaky) before firing
static const int STREAK_CAP = 3;        // max the leaky streak counter climbs to

static const wchar_t* MAIN_CLASS   = L"OvrkMainWnd";
static const wchar_t* REMIND_CLASS = L"OvrkRemindWnd";

#define IDT_POLL 1

#define WM_TRAYICON (WM_APP + 1)
#define IDM_TOGGLE   201
#define IDM_TEST     202
#define IDM_MORESENS 203
#define IDM_LESSSENS 204
#define IDM_WORKOUTS 205
#define IDM_SHOW     206
#define IDM_MOVE     207
#define IDM_EXIT     208

#define ID_BTN_DONE  301
#define HK_TRIGGER   1
#define HK_HIDE      2
#define HK_PAUSE     3
#define HK_QUIT      4
#define HK_MOVE      5

// palette
static const COLORREF C_BG       = RGB(21, 23, 30);
static const COLORREF C_BORDER   = RGB(44, 48, 62);
static const COLORREF C_TITLE    = RGB(236, 239, 246);
static const COLORREF C_SUB      = RGB(150, 156, 168);
static const COLORREF C_GREEN    = RGB(74, 208, 128);
static const COLORREF C_AMBER    = RGB(240, 190, 80);
static const COLORREF C_GREY     = RGB(120, 126, 138);
static const COLORREF C_BARBG    = RGB(38, 41, 52);
static const COLORREF C_BTN      = RGB(37, 41, 54);
static const COLORREF C_BTN_HOV  = RGB(50, 56, 73);
static const COLORREF C_BTN_TXT  = RGB(223, 228, 238);
static const COLORREF C_DOTS     = RGB(138, 144, 156);
static const COLORREF C_DOTS_HOV = RGB(214, 219, 229);

// ------------------------------------------------------------------- app state
struct Config {
    int threshold   = 60;   // end-screen confidence % needed to fire
    int pollMs      = 450;
    int cooldownSec = 25;   // min seconds between reminders
    int posX        = -1;   // saved overlay position (-1 = default top-right)
    int posY        = -1;
};

static HINSTANCE g_hInst  = nullptr;
static HWND      g_main   = nullptr;
static HWND      g_remind = nullptr;
static Config    g_cfg;
static std::vector<std::wstring> g_workouts;
static std::wstring g_currentWorkout;

static bool   g_moveMode  = false;   // overlay temporarily unlocked for dragging
static bool   g_watching  = true;
static bool   g_matched   = false;
static bool   g_robloxOpen = false;
static time_t g_lastFire  = 0;
static int    g_lastConf  = 0;
static int    g_hitStreak = 0;   // how many consecutive polls have detected the end screen

static NOTIFYICONDATAW g_nid = {};

// overlay is a click-through status HUD (no interactive controls)
static const int WIN_W = 224, WIN_H = 74;

// fonts
static HFONT g_fTitle=nullptr, g_fStatus=nullptr, g_fBtn=nullptr;
static HFONT g_fHuge=nullptr, g_fMid=nullptr, g_fSmall=nullptr;

// ------------------------------------------------------------------- utilities
static HFONT makeFont(int h, int weight) {
    return CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}
static std::wstring configDir() {
    wchar_t path[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        std::wstring dir = std::wstring(path) + L"\\OvrkWorkout";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return L".";
}
static std::wstring configFile()   { return configDir() + L"\\config.bin"; }
static std::wstring workoutsFile() { return configDir() + L"\\workouts.txt"; }

static void fillRound(HDC dc, RECT r, int rad, COLORREF c) {
    HRGN rg = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, rad, rad);
    HBRUSH b = CreateSolidBrush(c);
    FillRgn(dc, rg, b);
    DeleteObject(b); DeleteObject(rg);
}
static void frameRound(HDC dc, RECT r, int rad, COLORREF c) {
    HRGN rg = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, rad, rad);
    HBRUSH b = CreateSolidBrush(c);
    FrameRgn(dc, rg, b, 1, 1);
    DeleteObject(b); DeleteObject(rg);
}
static void fillCircle(HDC dc, int cx, int cy, int r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b);
}

// ------------------------------------------------------------------- workouts
static void writeDefaultWorkouts() {
    const char* def =
        "5 Push-ups\r\n"
        "5 Pull-ups\r\n"
        "5 Sit-ups\r\n"
        "10-second Plank\r\n"
        "5 Crunches\r\n";
    FILE* f = _wfopen(workoutsFile().c_str(), L"wb");
    if (f) { fwrite(def, 1, strlen(def), f); fclose(f); }
}
static void loadWorkouts() {
    g_workouts.clear();
    FILE* f = _wfopen(workoutsFile().c_str(), L"rb");
    if (!f) { writeDefaultWorkouts(); f = _wfopen(workoutsFile().c_str(), L"rb"); }
    if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::string buf; buf.resize(n > 0 ? n : 0);
    if (n > 0) fread(&buf[0], 1, n, f);
    fclose(f);
    std::string line;
    auto flush = [&]() {
        while (!line.empty() && (line.back()=='\r'||line.back()==' '||line.back()=='\t'))
            line.pop_back();
        if (!line.empty()) {
            int wl = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
            if (wl > 0) {
                std::wstring w(wl - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, &w[0], wl);
                g_workouts.push_back(w);
            }
        }
        line.clear();
    };
    for (char c : buf) { if (c=='\n') flush(); else line.push_back(c); }
    flush();
    if (g_workouts.empty()) g_workouts.push_back(L"20 Push-ups");
}

// ------------------------------------------------------------------- persistence
static void saveConfig() {
    FILE* f = _wfopen(configFile().c_str(), L"wb");
    if (!f) return;
    const char magic[4] = {'O','V','R','K'};
    int32_t version = 5, th = g_cfg.threshold, pm = g_cfg.pollMs, cd = g_cfg.cooldownSec;
    int32_t px = g_cfg.posX, py = g_cfg.posY;
    fwrite(magic, 1, 4, f); fwrite(&version,4,1,f);
    fwrite(&th,4,1,f); fwrite(&pm,4,1,f); fwrite(&cd,4,1,f);
    fwrite(&px,4,1,f); fwrite(&py,4,1,f);
    fclose(f);
}
static void loadConfig() {
    FILE* f = _wfopen(configFile().c_str(), L"rb");
    if (!f) return;
    char magic[4]; int32_t version=0, th=60, pm=450, cd=25, px=-1, py=-1;
    if (fread(magic,1,4,f)==4 && memcmp(magic,"OVRK",4)==0) {
        fread(&version,4,1,f);
        if (version == 5) {   // older versions reset to the new defaults
            fread(&th,4,1,f); fread(&pm,4,1,f); fread(&cd,4,1,f);
            fread(&px,4,1,f); fread(&py,4,1,f);
            g_cfg.threshold=th; g_cfg.pollMs=pm; g_cfg.cooldownSec=cd;
            g_cfg.posX=px; g_cfg.posY=py;
        }
    }
    fclose(f);
}

// ------------------------------------------------------------------- detection
// True only when the Roblox game client (RobloxPlayerBeta.exe) is running, so the
// reminder can never fire when you're not actually in Roblox. (RobloxCrashHandler
// and the launcher are deliberately ignored.)
static bool isRobloxRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsnicmp(pe.szExeFile, L"RobloxPlayerBeta", 16) == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// Downscale the whole primary screen into a GRID_W x GRID_H BGRA buffer.
static bool captureScreen(uint8_t* out) {
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw <= 0 || sh <= 0) return false;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;

    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = GRID_W;
    bi.bmiHeader.biHeight = -GRID_H;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, dib);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    // No CAPTUREBLT: our own layered overlays are excluded from the grab.
    StretchBlt(dc, 0, 0, GRID_W, GRID_H, screen, 0, 0, sw, sh, SRCCOPY);
    GdiFlush();
    bool ok = false;
    if (bits) { memcpy(out, bits, GRID_W * GRID_H * 4); ok = true; }
    SelectObject(dc, old); DeleteObject(dib); DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return ok;
}

// Confidence 0..100 that the OVERKILL match-end screen is showing.
//
// The Leave/Rematch buttons are two large, adjacent, similarly-sized, horizontally
// centred coloured rectangles (red left, purple right). We collect the red and purple
// pixels in the centre-bottom of the screen, take each colour's bounding box, and
// require the pair to actually be button-shaped and arranged correctly:
//   * each box is button-sized and wider than tall,
//   * each box is well filled with its colour (white button text only lowers the
//     fill a little; scattered game pixels give a big sparse box and are rejected),
//   * red is left of and right next to purple, the boxes line up vertically and are
//     similar in size, and the pair is centred on screen.
// Combined with the multi-poll confirmation in the caller, this is very specific.
static int detectEndScreen(const uint8_t* p) {
    // Bottom slice only: the Leave/Rematch buttons hug the bottom edge, below the
    // player character (whose red outfit would otherwise merge with the red button).
    int y0 = (int)(GRID_H * 0.83f), y1 = (int)(GRID_H * 0.99f);
    int x0 = (int)(GRID_W * 0.27f), x1 = (int)(GRID_W * 0.73f);   // centre band only

    int rN=0, rX0=GRID_W, rX1=-1, rY0=GRID_H, rY1=-1;
    int pN=0, pX0=GRID_W, pX1=-1, pY0=GRID_H, pY1=-1;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const uint8_t* px = p + (y * GRID_W + x) * 4;
            int b = px[0], g = px[1], r = px[2];
            if (r > 170 && g < 128 && b < 128 && r > g + 50 && r > b + 50) {                  // red
                rN++; if(x<rX0)rX0=x; if(x>rX1)rX1=x; if(y<rY0)rY0=y; if(y>rY1)rY1=y;
            } else if (b > 158 && r > 88 && r < 212 && g < 148 && b > g + 44 && r > g + 6) {   // purple
                pN++; if(x<pX0)pX0=x; if(x>pX1)pX1=x; if(y<pY0)pY0=y; if(y>pY1)pY1=y;
            }
        }
    const int minPix = 32;
    if (rN < minPix || pN < minPix) return 0;

    int rW=rX1-rX0+1, rH=rY1-rY0+1, pW=pX1-pX0+1, pH=pY1-pY0+1;
    const int minW=GRID_W/26, maxW=GRID_W/4, minH=7, maxH=GRID_H/6;
    if (!(rW>=minW && rW<=maxW && rH>=minH && rH<=maxH && rW>rH)) return 0;
    if (!(pW>=minW && pW<=maxW && pH>=minH && pH<=maxH && pW>pH)) return 0;

    double rFill=(double)rN/(rW*rH), pFill=(double)pN/(pW*pH);
    if (rFill < 0.35 || pFill < 0.35) return 0;

    if (rX1 > pX0 + 2) return 0;                       // red left of purple
    if (pX0 - rX1 > GRID_W/16) return 0;               // and right next to it
    int ov = (rY1<pY1?rY1:pY1) - (rY0>pY0?rY0:pY0) + 1;
    if (ov < 0.5*(rH<pH?rH:pH)) return 0;              // vertically aligned
    if (rH>pH*2.0 || pH>rH*2.0) return 0;              // similar heights
    if (rW>pW*2.6 || pW>rW*2.6) return 0;              // similar widths
    double cx = ((rX0+pX1)/2.0)/GRID_W;
    if (cx < 0.38 || cx > 0.62) return 0;              // centred on screen

    double q = rFill<pFill ? rFill : pFill;            // all gates passed
    int conf = 70 + (int)((q - 0.35) / 0.45 * 30.0);   // fill .35 -> 70,  .80 -> 100
    if (conf < 70) conf = 70; if (conf > 100) conf = 100;
    return conf;
}

// Append a diagnostic line whenever the screen looks end-screen-ish, so a missed
// match can be understood after the fact (detect_log.txt in the config folder).
static void logDetect(int conf, int streak, bool fired) {
    std::wstring path = configDir() + L"\\detect_log.txt";
    FILE* f = _wfopen(path.c_str(), L"a");
    if (!f) return;
    time_t t = time(nullptr); struct tm tmv; localtime_s(&tmv, &t);
    char ts[16]; strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
    fprintf(f, "%s  conf=%3d  streak=%d%s\n", ts, conf, streak, fired ? "   <== REMINDER FIRED" : "");
    fclose(f);
}

// ------------------------------------------------------------------- reminder
static void showReminder() {
    if (g_remind) return;
    if (g_workouts.empty()) loadWorkouts();
    g_currentWorkout = g_workouts[rand() % g_workouts.size()];

    int W = 560, H = 320;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    int x = (sx - W)/2, y = (sy - H)/3;
    g_remind = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        REMIND_CLASS, L"Workout time", WS_POPUP,
        x, y, W, H, nullptr, nullptr, g_hInst, nullptr);
    if (!g_remind) return;
    HRGN rgn = CreateRoundRectRgn(0, 0, W+1, H+1, 22, 22);
    SetWindowRgn(g_remind, rgn, TRUE);
    SetLayeredWindowAttributes(g_remind, 0, 248, LWA_ALPHA);
    ShowWindow(g_remind, SW_SHOW);
    SetForegroundWindow(g_remind);
    FLASHWINFO fi = { sizeof(fi), g_remind, FLASHW_ALL, 3, 0 };
    FlashWindowEx(&fi);
    MessageBeep(MB_ICONEXCLAMATION);
}

// ------------------------------------------------------------------- tray
static void addTray() {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_main; g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"OVRK Workout Reminder");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}
static void removeTray() { Shell_NotifyIconW(NIM_DELETE, &g_nid); }

static void cmdTogglePause() { g_watching = !g_watching; g_matched = false; InvalidateRect(g_main,nullptr,FALSE); }
static void cmdEditWorkouts() { ShellExecuteW(nullptr,L"open",L"notepad.exe",workoutsFile().c_str(),nullptr,SW_SHOW); }
static void cmdMoreSensitive() { if (g_cfg.threshold > 25) g_cfg.threshold -= 5; saveConfig(); InvalidateRect(g_main,nullptr,FALSE); }
static void cmdLessSensitive() { if (g_cfg.threshold < 90) g_cfg.threshold += 5; saveConfig(); InvalidateRect(g_main,nullptr,FALSE); }

// Toggle "move mode": unlock the overlay so it can be dragged with the mouse
// (removes click-through), or lock it back and remember where it was left.
static void setMoveMode(bool on) {
    g_moveMode = on;
    LONG ex = GetWindowLong(g_main, GWL_EXSTYLE);
    if (on) ex &= ~WS_EX_TRANSPARENT;   // draggable
    else    ex |=  WS_EX_TRANSPARENT;   // click-through again
    SetWindowLong(g_main, GWL_EXSTYLE, ex);
    SetWindowPos(g_main, HWND_TOPMOST, 0,0,0,0,
                 SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED);
    if (on) {
        ShowWindow(g_main, SW_SHOW);
        SetForegroundWindow(g_main);
    } else {
        RECT r; GetWindowRect(g_main, &r);   // remember the new spot
        g_cfg.posX = r.left; g_cfg.posY = r.top;
        saveConfig();
    }
    InvalidateRect(g_main, nullptr, FALSE);
}

static void showTrayMenu() {
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_TOGGLE, g_watching ? L"Pause watching\tCtrl+Alt+P" : L"Resume watching\tCtrl+Alt+P");
    AppendMenuW(m, MF_STRING, IDM_TEST,   L"Test reminder\tCtrl+Alt+W");
    AppendMenuW(m, MF_STRING, IDM_SHOW,   L"Hide / show overlay\tCtrl+Alt+H");
    AppendMenuW(m, MF_STRING, IDM_MOVE,   g_moveMode ? L"Lock overlay position\tCtrl+Alt+M"
                                                     : L"Move overlay\tCtrl+Alt+M");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_MORESENS, L"More sensitive");
    AppendMenuW(m, MF_STRING, IDM_LESSSENS, L"Less sensitive");
    AppendMenuW(m, MF_STRING, IDM_WORKOUTS, L"Edit workout list…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"Exit\tCtrl+Alt+X");
    SetForegroundWindow(g_main);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, nullptr);
    DestroyMenu(m);
}

// ------------------------------------------------------------------- reminder proc
static LRESULT CALLBACK RemindWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        CreateWindowW(L"BUTTON", L"DONE  ✓",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
            (560-190)/2, 250, 190, 48, hwnd, (HMENU)ID_BTN_DONE, g_hInst, nullptr);
        SendMessageW(GetDlgItem(hwnd, ID_BTN_DONE), WM_SETFONT, (WPARAM)g_fBtn, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(16,18,24)); FillRect(dc,&rc,bg); DeleteObject(bg);
        RECT top = {0,0,rc.right,6}; HBRUSH ac = CreateSolidBrush(C_GREEN); FillRect(dc,&top,ac); DeleteObject(ac);
        SetBkMode(dc, TRANSPARENT);
        RECT r1={0,34,rc.right,80}; SelectObject(dc,g_fHuge); SetTextColor(dc,RGB(240,240,245));
        DrawTextW(dc,L"TIME TO MOVE!",-1,&r1,DT_CENTER|DT_SINGLELINE);
        RECT r2={0,96,rc.right,122}; SelectObject(dc,g_fSmall); SetTextColor(dc,C_SUB);
        DrawTextW(dc,L"Knock this out before your next match:",-1,&r2,DT_CENTER|DT_SINGLELINE);
        RECT r3={20,140,rc.right-20,210}; SelectObject(dc,g_fMid); SetTextColor(dc,C_GREEN);
        DrawTextW(dc,g_currentWorkout.c_str(),-1,&r3,DT_CENTER|DT_WORDBREAK|DT_VCENTER);
        RECT r4={0,305,rc.right,320}; SelectObject(dc,g_fSmall); SetTextColor(dc,RGB(110,115,125));
        DrawTextW(dc,L"Press Enter when finished",-1,&r4,DT_CENTER|DT_SINGLELINE);
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_CTLCOLORBTN: return (LRESULT)GetStockObject(NULL_BRUSH);
    case WM_COMMAND: if (LOWORD(wp)==ID_BTN_DONE) { DestroyWindow(hwnd); return 0; } break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_remind = nullptr; g_lastFire = time(nullptr); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------- main proc
static void paintMain(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    HDC hdc = GetDC(hwnd);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(dc, bmp);

    // background + border (move mode = amber border to show it's unlocked)
    HBRUSH bg = CreateSolidBrush(C_BG); FillRect(dc, &rc, bg); DeleteObject(bg);
    frameRound(dc, rc, 16, g_moveMode ? C_AMBER : C_BORDER);
    RECT accent = {0,0,rc.right,3}; fillRound(dc, accent, 0, g_moveMode ? C_AMBER : RGB(90,170,255));

    SetBkMode(dc, TRANSPARENT);

    // status dot + title
    COLORREF dotCol = g_moveMode ? C_AMBER
                    : !g_watching ? C_GREY
                    : !g_robloxOpen ? C_GREY
                    : (g_matched ? C_AMBER : C_GREEN);
    fillCircle(dc, 20, 22, 5, dotCol);
    SelectObject(dc, g_fTitle); SetTextColor(dc, C_TITLE);
    SetTextCharacterExtra(dc, 1);
    TextOutW(dc, 34, 13, L"OVRK WORKOUT", 12);
    SetTextCharacterExtra(dc, 0);

    // status line
    const wchar_t* status = g_moveMode   ? L"Drag me · Ctrl+Alt+M to lock"
                          : !g_watching  ? L"Paused"
                          : !g_robloxOpen ? L"Waiting for Roblox…"
                          : g_matched     ? L"Match ended — move!"
                          : L"Watching for match end";
    SelectObject(dc, g_fStatus);
    SetTextColor(dc, g_moveMode ? C_AMBER
                   : (!g_watching || !g_robloxOpen) ? C_GREY
                   : (g_matched ? C_GREEN : C_SUB));
    TextOutW(dc, 16, 36, status, (int)wcslen(status));

    // confidence bar
    RECT bar = {16, 58, rc.right-16, 65};
    fillRound(dc, bar, 4, C_BARBG);
    int span = bar.right - bar.left;
    int fillw = g_watching ? (span * (g_lastConf>100?100:g_lastConf)) / 100 : 0;
    if (fillw > 0) {
        RECT fb = {bar.left, bar.top, bar.left + (fillw<8?8:fillw), bar.bottom};
        fillRound(dc, fb, 4, g_lastConf >= g_cfg.threshold ? C_GREEN : RGB(80,140,220));
    }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc);
    ReleaseDC(hwnd, hdc);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        addTray();
        SetTimer(hwnd, IDT_POLL, g_cfg.pollMs, nullptr);
        UINT mod = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
        RegisterHotKey(hwnd, HK_TRIGGER, mod, 'W');   // test / trigger now
        RegisterHotKey(hwnd, HK_HIDE,    mod, 'H');   // hide / show overlay
        RegisterHotKey(hwnd, HK_PAUSE,   mod, 'P');   // pause / resume watching
        RegisterHotKey(hwnd, HK_QUIT,    mod, 'X');   // close app entirely
        RegisterHotKey(hwnd, HK_MOVE,    mod, 'M');   // unlock / lock for dragging
        return 0;
    }
    case WM_TIMER:
        if (wp == IDT_POLL && g_watching && !g_remind) {
            g_robloxOpen = isRobloxRunning();
            if (!g_robloxOpen) {
                // Roblox isn't running — never fire, and idle the detector.
                g_lastConf = 0; g_matched = false; g_hitStreak = 0;
            } else {
                static uint8_t buf[GRID_W*GRID_H*4];
                if (captureScreen(buf)) {
                    int conf = detectEndScreen(buf);
                    g_lastConf = conf;
                    time_t now = time(nullptr);
                    bool fired = false;
                    if (conf >= g_cfg.threshold) {
                        // leaky confirmation: climbs on hits, decays on misses, so a
                        // one-frame mid-game flash can't reach REQUIRED_STREAK but the
                        // real screen does — and a single dropped frame won't reset it.
                        if (g_hitStreak < STREAK_CAP) g_hitStreak++;
                        if (g_hitStreak >= REQUIRED_STREAK && !g_matched
                            && (now - g_lastFire) >= g_cfg.cooldownSec) {
                            g_matched = true; g_lastFire = now; showReminder(); fired = true;
                        }
                    } else {
                        if (g_hitStreak > 0) g_hitStreak--;
                        if (g_hitStreak == 0) g_matched = false;
                    }
                    if (conf >= g_cfg.threshold - 20 || fired) logDetect(conf, g_hitStreak, fired);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_HOTKEY:
        switch (wp) {
        case HK_TRIGGER: showReminder(); return 0;
        case HK_HIDE:    ShowWindow(hwnd, IsWindowVisible(hwnd) ? SW_HIDE : SW_SHOW); return 0;
        case HK_PAUSE:   cmdTogglePause(); return 0;
        case HK_QUIT:    DestroyWindow(hwnd); return 0;
        case HK_MOVE:    setMoveMode(!g_moveMode); return 0;
        }
        return 0;

    case WM_LBUTTONDOWN:   // in move mode, drag the overlay by its body
        if (g_moveMode) { ReleaseCapture(); SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    case WM_KEYDOWN:
        if (g_moveMode && wp == VK_ESCAPE) setMoveMode(false);
        return 0;

    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd,&ps); paintMain(hwnd); EndPaint(hwnd,&ps); return 0; }
    case WM_ERASEBKGND: return 1;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TOGGLE:  cmdTogglePause(); return 0;
        case IDM_TEST:    showReminder(); return 0;
        case IDM_MORESENS: cmdMoreSensitive(); return 0;
        case IDM_LESSSENS: cmdLessSensitive(); return 0;
        case IDM_WORKOUTS: cmdEditWorkouts(); return 0;
        case IDM_SHOW:    ShowWindow(hwnd, IsWindowVisible(hwnd)?SW_HIDE:SW_SHOW); return 0;
        case IDM_MOVE:    setMoveMode(!g_moveMode); return 0;
        case IDM_EXIT:    DestroyWindow(hwnd); return 0;
        }
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lp)==WM_RBUTTONUP || LOWORD(lp)==WM_CONTEXTMENU) showTrayMenu();
        else if (LOWORD(lp)==WM_LBUTTONDBLCLK) { ShowWindow(hwnd,SW_SHOW); SetForegroundWindow(hwnd); }
        return 0;

    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, IDT_POLL);
        UnregisterHotKey(hwnd, HK_TRIGGER);
        UnregisterHotKey(hwnd, HK_HIDE);
        UnregisterHotKey(hwnd, HK_PAUSE);
        UnregisterHotKey(hwnd, HK_QUIT);
        UnregisterHotKey(hwnd, HK_MOVE);
        removeTray();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------- WinMain
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    g_hInst = hInst;
    SetProcessDPIAware();
    srand((unsigned)time(nullptr));

    g_fTitle  = makeFont(18, FW_BOLD);
    g_fStatus = makeFont(15, FW_NORMAL);
    g_fBtn    = makeFont(16, FW_SEMIBOLD);
    g_fHuge   = makeFont(40, FW_BOLD);
    g_fMid    = makeFont(30, FW_SEMIBOLD);
    g_fSmall  = makeFont(15, FW_NORMAL);

    loadWorkouts();
    loadConfig();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc; wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = MAIN_CLASS;
    RegisterClassW(&wc);

    WNDCLASSW wr = {};
    wr.lpfnWndProc = RemindWndProc; wr.hInstance = hInst;
    wr.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wr.lpszClassName = REMIND_CLASS;
    RegisterClassW(&wr);

    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    // use the saved position if we have one, else default to the top-right corner
    int wx = (g_cfg.posX >= 0) ? g_cfg.posX : sx - WIN_W - 24;
    int wy = (g_cfg.posY >= 0) ? g_cfg.posY : 24;
    if (wx < 0) wx = 0; if (wx > sx - WIN_W) wx = sx - WIN_W;   // keep on-screen
    if (wy < 0) wy = 0; if (wy > sy - WIN_H) wy = sy - WIN_H;
    // WS_EX_TRANSPARENT => click-through: the mouse passes straight to the game.
    g_main = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        MAIN_CLASS, L"OVRK Workout", WS_POPUP,
        wx, wy, WIN_W, WIN_H, nullptr, nullptr, hInst, nullptr);
    if (!g_main) return 1;
    HRGN rgn = CreateRoundRectRgn(0, 0, WIN_W+1, WIN_H+1, 16, 16);
    SetWindowRgn(g_main, rgn, TRUE);
    SetLayeredWindowAttributes(g_main, 0, 244, LWA_ALPHA);
    ShowWindow(g_main, SW_SHOW);
    UpdateWindow(g_main);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_remind && IsDialogMessageW(g_remind, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
