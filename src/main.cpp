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
#include <cstdlib>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <ctime>

// ------------------------------------------------------------------ constants
static const int GRID_W = 320;          // downscaled screen used for detection
static const int GRID_H = 180;
static const int REQUIRED_STREAK = 2;   // positive polls (leaky) before firing
static const int STREAK_CAP = 3;        // max the leaky streak counter climbs to
static const int REARM_SEC = 4;         // end screen must be gone this long before reminding again
static const DWORD REMIND_GUARD_MS = 800; // ignore input this long after a reminder appears

static const wchar_t* MAIN_CLASS     = L"OvrkMainWnd";
static const wchar_t* REMIND_CLASS   = L"OvrkRemindWnd";
static const wchar_t* SETTINGS_CLASS = L"OvrkSettingsWnd";

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
#define IDM_RESETPROG 209
#define IDM_SETTINGS  210
#define IDM_GOAL_BASE 230    // 230..235 = daily-goal presets

#define ID_BTN_DONE  301
#define HK_TRIGGER   1
#define HK_HIDE      2
#define HK_PAUSE     3
#define HK_QUIT      4
#define HK_MOVE      5
#define HK_SETTINGS  6

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
    int goal        = 100;  // daily rep goal
    int progress    = 0;    // reps done today
    int progDay     = 0;    // yyyymmdd the progress belongs to (for daily reset)
    int lifetime    = 0;    // all-time reps completed (never resets)
    int showKeys    = 0;    // 1 = show the shortcut list on the overlay
};

static HINSTANCE g_hInst  = nullptr;
static HWND      g_main   = nullptr;
static HWND      g_remind = nullptr;
static HWND      g_settings = nullptr;
static Config    g_cfg;
struct Workout { std::wstring text; bool enabled; };
static std::vector<Workout> g_workouts;
static std::wstring g_currentWorkout;

static bool   g_moveMode  = false;   // overlay temporarily unlocked for dragging
static bool   g_watching  = true;
static bool   g_reminded  = false;   // already reminded for the CURRENT end screen
static bool   g_robloxOpen = false;
static time_t g_lastHigh  = 0;       // last time detection confidence was above threshold
static int    g_lastConf  = 0;
static int    g_hitStreak = 0;   // leaky counter of positive detection polls
static DWORD  g_remindShownAt = 0;   // tick when the current reminder popup appeared
static bool   g_remindCounts  = false; // completing this reminder adds to the goal
static int    g_pendingReps   = 0;   // reps the current reminder is worth
static bool   g_doneHover     = false; // mouse over the reminder's DONE button
static bool   g_remindTrack   = false; // TrackMouseEvent armed on the reminder

static NOTIFYICONDATAW g_nid = {};

// overlay is a click-through status HUD (no interactive controls)
static const int WIN_W = 236;
static const int HUD_BASE_H = 96;    // title + status + goal tracker
static const int HUD_KEYS_H = 120;   // extra when the shortcut list is shown
static int hudHeight() { return HUD_BASE_H + (g_cfg.showKeys ? HUD_KEYS_H : 0); }

// fonts
static HFONT g_fTitle=nullptr, g_fStatus=nullptr, g_fBtn=nullptr;
static HFONT g_fHuge=nullptr, g_fMid=nullptr, g_fSmall=nullptr;
static HFONT g_fTiny=nullptr, g_fNum=nullptr, g_fDone=nullptr;

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
    // strip a UTF-8 BOM if an editor added one (else the first line won't parse)
    if (buf.size() >= 3 && (unsigned char)buf[0]==0xEF && (unsigned char)buf[1]==0xBB && (unsigned char)buf[2]==0xBF)
        buf.erase(0, 3);
    std::string line;
    auto flush = [&]() {
        while (!line.empty() && (line.back()=='\r'||line.back()==' '||line.back()=='\t'))
            line.pop_back();
        // a leading '#' marks a workout that's toggled OFF (still listed in settings)
        bool enabled = true;
        size_t s = 0;
        while (s < line.size() && (line[s]==' '||line[s]=='\t')) s++;
        if (s < line.size() && line[s]=='#') { enabled = false; s++;
            while (s < line.size() && line[s]==' ') s++; }
        std::string body = line.substr(s);
        if (!body.empty()) {
            int wl = MultiByteToWideChar(CP_UTF8, 0, body.c_str(), -1, nullptr, 0);
            if (wl > 0) {
                std::wstring w(wl - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, body.c_str(), -1, &w[0], wl);
                g_workouts.push_back({w, enabled});
            }
        }
        line.clear();
    };
    for (char c : buf) { if (c=='\n') flush(); else line.push_back(c); }
    flush();
    if (g_workouts.empty()) g_workouts.push_back({L"20 Push-ups", true});
}
// rewrite workouts.txt, prefixing disabled ones with "# "
static void saveWorkouts() {
    FILE* f = _wfopen(workoutsFile().c_str(), L"wb");
    if (!f) return;
    for (auto& wk : g_workouts) {
        std::string body;
        int need = WideCharToMultiByte(CP_UTF8, 0, wk.text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (need > 0) { body.resize(need - 1);
            WideCharToMultiByte(CP_UTF8, 0, wk.text.c_str(), -1, &body[0], need, nullptr, nullptr); }
        if (!wk.enabled) fwrite("# ", 1, 2, f);
        fwrite(body.c_str(), 1, body.size(), f);
        fwrite("\r\n", 1, 2, f);
    }
    fclose(f);
}
// change the leading number of a workout ("5 Push-ups" -> "8 Push-ups",
// "10-second Plank" -> "15-second Plank"). Timed exercises step by 5, reps by 1.
static void adjustWorkout(int i, int sign) {
    std::wstring& t = g_workouts[i].text;
    size_t p = 0; while (p < t.size() && iswspace(t[p])) p++;
    size_t d0 = p; while (p < t.size() && iswdigit(t[p])) p++;
    if (p == d0) return;                        // no leading number to change
    int num = _wtoi(t.c_str() + d0);
    std::wstring low = t; for (auto& c : low) c = towlower(c);
    bool timed = low.find(L"sec") != std::wstring::npos || low.find(L"min") != std::wstring::npos;
    num += sign * (timed ? 5 : 1);
    if (num < 1) num = 1; if (num > 999) num = 999;
    t = t.substr(0, d0) + std::to_wstring(num) + t.substr(p);
    saveWorkouts();
}

// pick a random ENABLED workout (falls back to any if none are enabled)
static const std::wstring& pickWorkout() {
    std::vector<int> on;
    for (int i = 0; i < (int)g_workouts.size(); ++i) if (g_workouts[i].enabled) on.push_back(i);
    if (!on.empty()) return g_workouts[on[rand() % on.size()]].text;
    return g_workouts[rand() % g_workouts.size()].text;
}

// ------------------------------------------------------------------- persistence
static void saveConfig() {
    FILE* f = _wfopen(configFile().c_str(), L"wb");
    if (!f) return;
    const char magic[4] = {'O','V','R','K'};
    int32_t version = 7, th = g_cfg.threshold, pm = g_cfg.pollMs, cd = g_cfg.cooldownSec;
    int32_t px = g_cfg.posX, py = g_cfg.posY;
    int32_t gl = g_cfg.goal, pr = g_cfg.progress, pd = g_cfg.progDay;
    int32_t lt = g_cfg.lifetime, sk = g_cfg.showKeys;
    fwrite(magic, 1, 4, f); fwrite(&version,4,1,f);
    fwrite(&th,4,1,f); fwrite(&pm,4,1,f); fwrite(&cd,4,1,f);
    fwrite(&px,4,1,f); fwrite(&py,4,1,f);
    fwrite(&gl,4,1,f); fwrite(&pr,4,1,f); fwrite(&pd,4,1,f);
    fwrite(&lt,4,1,f); fwrite(&sk,4,1,f);
    fclose(f);
}
static void loadConfig() {
    FILE* f = _wfopen(configFile().c_str(), L"rb");
    if (!f) return;
    char magic[4];
    int32_t version=0, th=60, pm=450, cd=25, px=-1, py=-1, gl=100, pr=0, pd=0, lt=0, sk=0;
    if (fread(magic,1,4,f)==4 && memcmp(magic,"OVRK",4)==0) {
        fread(&version,4,1,f);
        if (version == 7) {   // older versions reset to the new defaults
            fread(&th,4,1,f); fread(&pm,4,1,f); fread(&cd,4,1,f);
            fread(&px,4,1,f); fread(&py,4,1,f);
            fread(&gl,4,1,f); fread(&pr,4,1,f); fread(&pd,4,1,f);
            fread(&lt,4,1,f); fread(&sk,4,1,f);
            g_cfg.threshold=th; g_cfg.pollMs=pm; g_cfg.cooldownSec=cd;
            g_cfg.posX=px; g_cfg.posY=py;
            g_cfg.goal=gl; g_cfg.progress=pr; g_cfg.progDay=pd;
            g_cfg.lifetime=lt; g_cfg.showKeys=sk;
        }
    }
    fclose(f);
}
// today's date as yyyymmdd, for the daily progress reset
static int todayInt() {
    time_t t = time(nullptr); struct tm tmv; localtime_s(&tmv, &t);
    return (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday;
}
static void checkDayRollover() {
    int d = todayInt();
    if (g_cfg.progDay != d) { g_cfg.progDay = d; g_cfg.progress = 0; saveConfig(); }
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
// Strategy: ANCHOR on the red "Leave" button, then look for the purple "Rematch"
// button right next to it. The red button is reliably clean (little red on most
// maps), so once we have it we only search for purple in the small region just to
// its right — which ignores background purple everywhere else (crucial on purple/
// magenta maps where the whole scene is violet). Both must be button-shaped,
// well-filled, similarly sized, vertically aligned and centred on screen.
static int detectEndScreen(const uint8_t* p) {
    // Bottom slice only: the buttons hug the bottom edge, below the player character.
    int y0 = (int)(GRID_H * 0.83f), y1 = (int)(GRID_H * 0.99f);
    int cx0 = (int)(GRID_W * 0.27f), cx1 = (int)(GRID_W * 0.73f);   // centre columns
    const int minPix = 32;
    const int minW = GRID_W/26, maxW = GRID_W/4, minH = 7, maxH = GRID_H/6;

    // ---- 1. locate the red "Leave" button ----
    int rN=0, rX0=GRID_W, rX1=-1, rY0=GRID_H, rY1=-1;
    for (int y = y0; y < y1; ++y)
        for (int x = cx0; x < cx1; ++x) {
            const uint8_t* px = p + (y * GRID_W + x) * 4;
            int b = px[0], g = px[1], r = px[2];
            if (r > 170 && g < 128 && b < 128 && r > g + 50 && r > b + 50) {
                rN++; if(x<rX0)rX0=x; if(x>rX1)rX1=x; if(y<rY0)rY0=y; if(y>rY1)rY1=y;
            }
        }
    if (rN < minPix) return 0;
    int rW=rX1-rX0+1, rH=rY1-rY0+1;
    double rFill = (double)rN/(rW*rH);
    if (!(rW>=minW && rW<=maxW && rH>=minH && rH<=maxH && rW>rH && rFill>=0.35)) return 0;

    // ---- 2. find the purple "Rematch" button anchored just RIGHT of red ----
    int sxL = rX1 - 2;                    if (sxL < 0) sxL = 0;
    int sxR = rX1 + (int)(2.4 * rW);      if (sxR > GRID_W) sxR = GRID_W;
    int syT = rY0 - 3;                    if (syT < 0) syT = 0;
    int syB = rY1 + 4;                    if (syB > GRID_H) syB = GRID_H;
    int pN=0, pX0=GRID_W, pX1=-1, pY0=GRID_H, pY1=-1;
    for (int y = syT; y < syB; ++y)
        for (int x = sxL; x < sxR; ++x) {
            const uint8_t* px = p + (y * GRID_W + x) * 4;
            int b = px[0], g = px[1], r = px[2];
            // strong BLUE dominance: the button is cool blue-violet, unlike warm
            // magenta/lavender map purples (where red ~= blue)
            if (b > 160 && b > r + 35 && b > g + 45 && g < 150) {
                pN++; if(x<pX0)pX0=x; if(x>pX1)pX1=x; if(y<pY0)pY0=y; if(y>pY1)pY1=y;
            }
        }
    if (pN < minPix) return 0;
    int pW=pX1-pX0+1, pH=pY1-pY0+1;
    double pFill = (double)pN/(pW*pH);
    if (!(pW>=minW && pW<=maxW && pH>=minH && pH<=maxH && pW>pH && pFill>=0.35)) return 0;

    // ---- 3. the pair must sit side by side, aligned, similar size, centred ----
    if (pX0 - rX1 > GRID_W/16) return 0;               // adjacent
    int ov = (rY1<pY1?rY1:pY1) - (rY0>pY0?rY0:pY0) + 1;
    if (ov < 0.5*(rH<pH?rH:pH)) return 0;              // vertically aligned
    if (rH>pH*2.0 || pH>rH*2.0) return 0;              // similar heights
    if (rW>pW*2.6 || pW>rW*2.6) return 0;              // similar widths
    double cx = ((rX0+pX1)/2.0)/GRID_W;
    if (cx < 0.38 || cx > 0.62) return 0;              // centred on screen

    double q = rFill<pFill ? rFill : pFill;
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
// counts = whether finishing this reminder adds to the daily goal (real match-end
// reminders count; the manual test does not).
static void showReminder(bool counts) {
    if (g_remind) return;
    if (g_workouts.empty()) loadWorkouts();
    g_currentWorkout = pickWorkout();
    g_pendingReps = _wtoi(g_currentWorkout.c_str());   // leading number, e.g. "5 Push-ups" -> 5
    g_remindCounts = counts;
    g_doneHover = false; g_remindTrack = false;

    int W = 560, H = 340;
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
    g_remindShownAt = GetTickCount();
    ShowWindow(g_remind, SW_SHOW);
    SetForegroundWindow(g_remind);
    SetFocus(g_remind);
    FLASHWINFO fi = { sizeof(fi), g_remind, FLASHW_ALL, 3, 0 };
    FlashWindowEx(&fi);
    MessageBeep(MB_ICONEXCLAMATION);
}
// DONE-button geometry inside the 560x340 reminder
static RECT doneRect() { RECT r = { (560-210)/2, 258, (560+210)/2, 310 }; return r; }
// finish the current reminder: bank the reps (if it counts) and close
static void completeReminder(HWND hwnd) {
    if (g_remindCounts) {
        checkDayRollover();
        g_cfg.progress += g_pendingReps;
        g_cfg.lifetime += g_pendingReps;   // all-time total, auto-saved
        saveConfig();
        g_remindCounts = false;
        InvalidateRect(g_main, nullptr, FALSE);
        if (g_settings) InvalidateRect(g_settings, nullptr, FALSE);
    }
    DestroyWindow(hwnd);
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

static void cmdTogglePause() { g_watching = !g_watching; g_reminded = false; g_hitStreak = 0; InvalidateRect(g_main,nullptr,FALSE); }
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
    AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"Settings…\tCtrl+Alt+S");
    AppendMenuW(m, MF_STRING, IDM_SHOW,   L"Hide / show overlay\tCtrl+Alt+H");
    AppendMenuW(m, MF_STRING, IDM_MOVE,   g_moveMode ? L"Lock overlay position\tCtrl+Alt+M"
                                                     : L"Move overlay\tCtrl+Alt+M");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    // daily goal submenu
    static const int presets[6] = {25, 50, 75, 100, 150, 200};
    HMENU goalMenu = CreatePopupMenu();
    for (int i = 0; i < 6; ++i) {
        wchar_t lbl[24]; swprintf_s(lbl, L"%d reps", presets[i]);
        AppendMenuW(goalMenu, MF_STRING | (g_cfg.goal==presets[i] ? MF_CHECKED : 0),
                    IDM_GOAL_BASE + i, lbl);
    }
    AppendMenuW(m, MF_POPUP, (UINT_PTR)goalMenu, L"Daily goal");
    AppendMenuW(m, MF_STRING, IDM_RESETPROG, L"Reset today's progress");
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
static bool remindGuardPassed() { return GetTickCount() - g_remindShownAt >= REMIND_GUARD_MS; }

static LRESULT CALLBACK RemindWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC dc = CreateCompatibleDC(hdc);                 // double-buffer (hover repaints)
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(dc, bmp);

        HBRUSH bg = CreateSolidBrush(RGB(16,18,24)); FillRect(dc,&rc,bg); DeleteObject(bg);
        RECT top = {0,0,rc.right,6}; fillRound(dc, top, 0, C_GREEN);
        SetBkMode(dc, TRANSPARENT);

        RECT r1={0,30,rc.right,76}; SelectObject(dc,g_fHuge); SetTextColor(dc,RGB(240,240,245));
        DrawTextW(dc,L"TIME TO MOVE!",-1,&r1,DT_CENTER|DT_SINGLELINE);
        RECT r2={0,86,rc.right,110}; SelectObject(dc,g_fSmall); SetTextColor(dc,C_SUB);
        DrawTextW(dc,L"Knock this out before your next match:",-1,&r2,DT_CENTER|DT_SINGLELINE);
        RECT r3={20,118,rc.right-20,182}; SelectObject(dc,g_fMid); SetTextColor(dc,C_GREEN);
        DrawTextW(dc,g_currentWorkout.c_str(),-1,&r3,DT_CENTER|DT_WORDBREAK|DT_VCENTER);

        // daily goal progress
        int prog=g_cfg.progress, goal=g_cfg.goal>0?g_cfg.goal:1;
        int pct=prog*100/goal; if(pct>100)pct=100;
        RECT lbl={70,196,rc.right-70,216}; SelectObject(dc,g_fTiny);
        SetTextColor(dc,C_SUB); DrawTextW(dc,L"TODAY",-1,&lbl,DT_LEFT|DT_SINGLELINE);
        wchar_t cnt[32]; swprintf_s(cnt,L"%d / %d",prog,g_cfg.goal);
        SetTextColor(dc, prog>=g_cfg.goal?C_AMBER:C_GREEN); DrawTextW(dc,cnt,-1,&lbl,DT_RIGHT|DT_SINGLELINE);
        RECT bar={70,222,rc.right-70,230}; fillRound(dc,bar,4,C_BARBG);
        int span=bar.right-bar.left, fw=span*pct/100;
        if(fw>0){ RECT fb={bar.left,bar.top,bar.left+(fw<8?8:fw),bar.bottom};
                  fillRound(dc,fb,4, prog>=g_cfg.goal?C_AMBER:C_GREEN); }

        // clean DONE pill
        RECT d=doneRect();
        fillRound(dc, d, 16, g_doneHover ? RGB(92,216,144) : RGB(66,198,122));
        SelectObject(dc,g_fDone); SetTextColor(dc, RGB(12,24,16));
        DrawTextW(dc, L"DONE  ✓", -1, &d, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        RECT hint={0,318,rc.right,336}; SelectObject(dc,g_fTiny); SetTextColor(dc,RGB(110,115,125));
        DrawTextW(dc,L"Enter = done    ·    Esc = skip",-1,&hint,DT_CENTER|DT_SINGLELINE);

        BitBlt(hdc,0,0,rc.right,rc.bottom,dc,0,0,SRCCOPY);
        SelectObject(dc,old); DeleteObject(bmp); DeleteDC(dc);
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        RECT d=doneRect(); POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        bool h = PtInRect(&d,pt);
        if (h!=g_doneHover){ g_doneHover=h; InvalidateRect(hwnd,nullptr,FALSE); }
        if (!g_remindTrack){ TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,hwnd,0}; TrackMouseEvent(&t); g_remindTrack=true; }
        return 0;
    }
    case WM_MOUSELEAVE: g_remindTrack=false; if(g_doneHover){g_doneHover=false;InvalidateRect(hwnd,nullptr,FALSE);} return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp)==HTCLIENT && g_doneHover){ SetCursor(LoadCursor(nullptr,IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN: {
        if (!remindGuardPassed()) return 0;
        RECT d=doneRect(); POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if (PtInRect(&d,pt)) completeReminder(hwnd);
        return 0;
    }
    case WM_KEYDOWN:
        if (!remindGuardPassed()) return 0;
        if (wp==VK_RETURN || wp==VK_SPACE) completeReminder(hwnd);   // done (counts)
        else if (wp==VK_ESCAPE) DestroyWindow(hwnd);                 // skip (no count)
        return 0;
    case WM_CLOSE:
        if (!remindGuardPassed()) return 0;
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_remind = nullptr; g_remindCounts = false; return 0;
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
                    : (g_reminded ? C_GREEN : C_GREEN);
    fillCircle(dc, 20, 22, 5, dotCol);
    SelectObject(dc, g_fTitle); SetTextColor(dc, C_TITLE);
    SetTextCharacterExtra(dc, 1);
    TextOutW(dc, 34, 13, L"OVRK WORKOUT", 12);
    SetTextCharacterExtra(dc, 0);

    // status line
    const wchar_t* status = g_moveMode   ? L"Drag me · Ctrl+Alt+M to lock"
                          : !g_watching  ? L"Paused"
                          : !g_robloxOpen ? L"Waiting for Roblox…"
                          : g_reminded    ? L"Match ended ✓ — reminded"
                          : L"Watching for match end";
    SelectObject(dc, g_fStatus);
    SetTextColor(dc, g_moveMode ? C_AMBER
                   : (!g_watching || !g_robloxOpen) ? C_GREY
                   : (g_reminded ? C_GREEN : C_SUB));
    TextOutW(dc, 16, 36, status, (int)wcslen(status));

    // daily goal tracker
    int prog = g_cfg.progress, goal = g_cfg.goal > 0 ? g_cfg.goal : 1;
    int pct = prog * 100 / goal; if (pct > 100) pct = 100;
    bool done = prog >= g_cfg.goal;
    RECT lbl = {16, 56, rc.right-16, 74};
    SelectObject(dc, g_fTiny); SetTextColor(dc, C_SUB);
    DrawTextW(dc, done ? L"DAILY GOAL ✓" : L"DAILY GOAL", -1, &lbl, DT_LEFT|DT_SINGLELINE|DT_VCENTER);
    wchar_t cnt[32]; swprintf_s(cnt, L"%d / %d", prog, g_cfg.goal);
    SelectObject(dc, g_fNum); SetTextColor(dc, done ? C_AMBER : C_GREEN);
    DrawTextW(dc, cnt, -1, &lbl, DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
    RECT bar = {16, 79, rc.right-16, 87};
    fillRound(dc, bar, 4, C_BARBG);
    int span = bar.right - bar.left, fillw = span * pct / 100;
    if (fillw > 0) {
        RECT fb = {bar.left, bar.top, bar.left + (fillw<8?8:fillw), bar.bottom};
        fillRound(dc, fb, 4, done ? C_AMBER : C_GREEN);
    }

    // optional shortcut list
    if (g_cfg.showKeys) {
        RECT sep = {16, 97, rc.right-16, 98}; fillRound(dc, sep, 0, C_BORDER);
        int ky = 102;
        SelectObject(dc, g_fTiny); SetTextColor(dc, C_SUB);
        TextOutW(dc, 16, ky, L"SHORTCUTS", 9); ky += 20;
        static const wchar_t* K[6][2] = {
            {L"Ctrl+Alt+W", L"Test reminder"}, {L"Ctrl+Alt+S", L"Settings"},
            {L"Ctrl+Alt+H", L"Hide / show"},   {L"Ctrl+Alt+M", L"Move overlay"},
            {L"Ctrl+Alt+P", L"Pause"},         {L"Ctrl+Alt+X", L"Quit"},
        };
        for (int i = 0; i < 6; ++i) {
            SetTextColor(dc, RGB(120,200,255));
            TextOutW(dc, 16, ky, K[i][0], (int)wcslen(K[i][0]));
            SetTextColor(dc, C_SUB);
            TextOutW(dc, 110, ky, K[i][1], (int)wcslen(K[i][1]));
            ky += 16;
        }
    }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc);
    ReleaseDC(hwnd, hdc);
}

// grow/shrink the overlay to fit the optional shortcut list
static void updateHudSize() {
    if (!g_main) return;
    int h = hudHeight();
    RECT r; GetWindowRect(g_main, &r);
    SetWindowPos(g_main, HWND_TOPMOST, 0, 0, WIN_W, h, SWP_NOMOVE);
    HRGN rgn = CreateRoundRectRgn(0, 0, WIN_W+1, h+1, 16, 16);
    SetWindowRgn(g_main, rgn, TRUE);
    InvalidateRect(g_main, nullptr, TRUE);
}

// -------------------------------------------------------------- settings window
static int  g_setHover = 0;      // hovered element: 1000+i workout, -1/-2 goal, -3 keys
static bool g_setTrack = false;

struct SettingsLayout {
    int  nwk;
    RECT wkRow[24], wkBox[24], wkMinus[24], wkPlus[24];
    RECT goalMinus, goalPlus;  int goalRowY;
    int  allTimeY;
    RECT keysRow, keysSwitch;  int keysRowY;
    int  footerY, height;
};
static SettingsLayout setLayout(int cw) {
    SettingsLayout L; L.nwk = (int)g_workouts.size(); if (L.nwk > 24) L.nwk = 24;
    int pad = 20, y = 56;
    y += 24;                                   // "WORKOUTS" label
    for (int i = 0; i < L.nwk; ++i) {
        L.wkRow[i]   = { pad, y, cw - pad, y + 30 };
        L.wkBox[i]   = { pad, y + 4, pad + 22, y + 26 };
        L.wkMinus[i] = { cw - pad - 52, y + 3, cw - pad - 28, y + 27 };
        L.wkPlus[i]  = { cw - pad - 24, y + 3, cw - pad,      y + 27 };
        y += 32;
    }
    y += 16;
    L.goalRowY  = y;
    L.goalMinus = { cw - pad - 92, y - 2, cw - pad - 66, y + 24 };
    L.goalPlus  = { cw - pad - 26, y - 2, cw - pad,      y + 24 };
    y += 42;
    L.allTimeY  = y; y += 42;
    L.keysRowY  = y;
    L.keysRow   = { pad, y - 2, cw - pad, y + 24 };
    L.keysSwitch= { cw - pad - 46, y, cw - pad, y + 24 };
    y += 42;
    L.footerY   = y; y += 28;
    L.height    = y;
    return L;
}
static int setHitTest(const SettingsLayout& L, POINT p) {
    for (int i = 0; i < L.nwk; ++i) {
        if (g_workouts[i].enabled) {
            if (PtInRect(&L.wkMinus[i], p)) return 2000 + i;   // amount −
            if (PtInRect(&L.wkPlus[i],  p)) return 3000 + i;   // amount +
        }
        if (PtInRect(&L.wkRow[i], p)) return 1000 + i;         // toggle on/off
    }
    if (PtInRect(&L.goalMinus, p)) return -1;
    if (PtInRect(&L.goalPlus,  p)) return -2;
    if (PtInRect(&L.keysRow,   p)) return -3;
    return 0;
}
static void drawCheck(HDC dc, RECT b, bool on, bool hover) {
    if (on) {
        fillRound(dc, b, 6, hover ? RGB(92,216,144) : C_GREEN);
        SelectObject(dc, g_fStatus); SetTextColor(dc, RGB(12,24,16));
        DrawTextW(dc, L"✓", -1, &b, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    } else {
        fillRound(dc, b, 6, C_BTN);
        frameRound(dc, b, 6, hover ? RGB(120,126,138) : C_BORDER);
    }
}
static void drawSwitch(HDC dc, RECT r, bool on) {
    int h = r.bottom - r.top, kr = h/2 - 3;
    fillRound(dc, r, h/2, on ? C_GREEN : RGB(60,64,78));
    int kcx = on ? r.right - kr - 3 : r.left + kr + 3;
    fillCircle(dc, kcx, (r.top + r.bottom)/2, kr, RGB(242,244,250));
}
static void drawStepBtn(HDC dc, RECT r, const wchar_t* s, bool hover) {
    fillRound(dc, r, 8, hover ? C_BTN_HOV : C_BTN);
    SelectObject(dc, g_fNum); SetTextColor(dc, C_BTN_TXT);
    DrawTextW(dc, s, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        SettingsLayout L = setLayout(rc.right);
        HDC dc = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(dc, bmp);
        HBRUSH bg = CreateSolidBrush(C_BG); FillRect(dc, &rc, bg); DeleteObject(bg);
        frameRound(dc, rc, 18, C_BORDER);
        RECT ac = {0,0,rc.right,3}; fillRound(dc, ac, 0, RGB(90,170,255));
        SetBkMode(dc, TRANSPARENT);

        SelectObject(dc, g_fTitle); SetTextColor(dc, C_TITLE);
        SetTextCharacterExtra(dc, 1); TextOutW(dc, 20, 18, L"OVRK SETTINGS", 13);
        SetTextCharacterExtra(dc, 0);

        SelectObject(dc, g_fTiny); SetTextColor(dc, C_SUB);
        TextOutW(dc, 20, 58, L"WORKOUTS", 8);
        for (int i = 0; i < L.nwk; ++i) {
            bool on = g_workouts[i].enabled;
            drawCheck(dc, L.wkBox[i], on, g_setHover == 1000 + i);
            SelectObject(dc, g_fStatus);
            SetTextColor(dc, on ? C_TITLE : C_GREY);
            int textRight = on ? L.wkMinus[i].left - 10 : rc.right - 20;
            RECT tr = { L.wkBox[i].right + 12, L.wkRow[i].top, textRight, L.wkRow[i].bottom };
            DrawTextW(dc, g_workouts[i].text.c_str(), -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            if (on) {   // amount / time stepper
                drawStepBtn(dc, L.wkMinus[i], L"–", g_setHover == 2000 + i);
                drawStepBtn(dc, L.wkPlus[i],  L"+", g_setHover == 3000 + i);
            }
        }

        // daily goal
        SelectObject(dc, g_fStatus); SetTextColor(dc, C_TITLE);
        TextOutW(dc, 20, L.goalRowY, L"Daily goal", 10);
        drawStepBtn(dc, L.goalMinus, L"–", g_setHover==-1);
        drawStepBtn(dc, L.goalPlus,  L"+", g_setHover==-2);
        wchar_t gv[16]; swprintf_s(gv, L"%d", g_cfg.goal);
        SelectObject(dc, g_fNum); SetTextColor(dc, C_GREEN);
        RECT gvr = { L.goalMinus.right, L.goalRowY-2, L.goalPlus.left, L.goalRowY+24 };
        DrawTextW(dc, gv, -1, &gvr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        // all-time total
        SelectObject(dc, g_fStatus); SetTextColor(dc, C_TITLE);
        TextOutW(dc, 20, L.allTimeY, L"All-time reps", 13);
        wchar_t lt[24]; swprintf_s(lt, L"%d", g_cfg.lifetime);
        SelectObject(dc, g_fNum); SetTextColor(dc, C_AMBER);
        RECT ltr = { 20, L.allTimeY-2, rc.right-20, L.allTimeY+24 };
        DrawTextW(dc, lt, -1, &ltr, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

        // show shortcuts toggle
        SelectObject(dc, g_fStatus); SetTextColor(dc, C_TITLE);
        TextOutW(dc, 20, L.keysRowY, L"Show shortcuts on overlay", 25);
        drawSwitch(dc, L.keysSwitch, g_cfg.showKeys != 0);

        SelectObject(dc, g_fTiny); SetTextColor(dc, RGB(110,115,125));
        RECT fr = {0, L.footerY, rc.right, L.footerY+18};
        DrawTextW(dc, L"Changes save automatically  ·  Esc or Ctrl+Alt+S to close", -1, &fr, DT_CENTER|DT_SINGLELINE);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc);
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        RECT rc; GetClientRect(hwnd, &rc);
        SettingsLayout L = setLayout(rc.right);
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = setHitTest(L, p);
        if (h != g_setHover) { g_setHover = h; InvalidateRect(hwnd, nullptr, FALSE); }
        if (!g_setTrack) { TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,hwnd,0}; TrackMouseEvent(&t); g_setTrack=true; }
        return 0;
    }
    case WM_MOUSELEAVE: g_setTrack=false; if(g_setHover){g_setHover=0;InvalidateRect(hwnd,nullptr,FALSE);} return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp)==HTCLIENT && g_setHover) { SetCursor(LoadCursor(nullptr,IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN: {
        RECT rc; GetClientRect(hwnd, &rc);
        SettingsLayout L = setLayout(rc.right);
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int code = setHitTest(L, p);
        if (code >= 3000) {
            adjustWorkout(code - 3000, +1); InvalidateRect(hwnd, nullptr, FALSE);
        } else if (code >= 2000) {
            adjustWorkout(code - 2000, -1); InvalidateRect(hwnd, nullptr, FALSE);
        } else if (code >= 1000) {
            int i = code - 1000;
            g_workouts[i].enabled = !g_workouts[i].enabled;
            saveWorkouts(); InvalidateRect(hwnd, nullptr, FALSE);
        } else if (code == -1) {
            g_cfg.goal -= 5; if (g_cfg.goal < 5) g_cfg.goal = 5;
            saveConfig(); InvalidateRect(hwnd,nullptr,FALSE); InvalidateRect(g_main,nullptr,FALSE);
        } else if (code == -2) {
            g_cfg.goal += 5; if (g_cfg.goal > 1000) g_cfg.goal = 1000;
            saveConfig(); InvalidateRect(hwnd,nullptr,FALSE); InvalidateRect(g_main,nullptr,FALSE);
        } else if (code == -3) {
            g_cfg.showKeys = g_cfg.showKeys ? 0 : 1;
            saveConfig(); updateHudSize(); InvalidateRect(hwnd,nullptr,FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN: if (wp==VK_ESCAPE) DestroyWindow(hwnd); return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_settings = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void openSettings() {
    if (g_settings) { SetForegroundWindow(g_settings); return; }
    int cw = 360;
    int ch = setLayout(cw).height;
    int sx=GetSystemMetrics(SM_CXSCREEN), sy=GetSystemMetrics(SM_CYSCREEN);
    g_settings = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        SETTINGS_CLASS, L"OVRK Settings", WS_POPUP,
        (sx-cw)/2, (sy-ch)/2, cw, ch, nullptr, nullptr, g_hInst, nullptr);
    if (!g_settings) return;
    HRGN rgn = CreateRoundRectRgn(0, 0, cw+1, ch+1, 18, 18);
    SetWindowRgn(g_settings, rgn, TRUE);
    SetLayeredWindowAttributes(g_settings, 0, 250, LWA_ALPHA);
    g_setHover = 0; g_setTrack = false;
    ShowWindow(g_settings, SW_SHOW);
    SetForegroundWindow(g_settings); SetFocus(g_settings);
}
static void toggleSettings() { if (g_settings) DestroyWindow(g_settings); else openSettings(); }

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
        RegisterHotKey(hwnd, HK_SETTINGS,mod, 'S');   // open / close settings
        return 0;
    }
    case WM_TIMER:
        if (wp == IDT_POLL && g_watching && !g_remind) {
            checkDayRollover();
            g_robloxOpen = isRobloxRunning();
            if (!g_robloxOpen) {
                // Roblox isn't running — never fire, and idle the detector.
                g_lastConf = 0; g_reminded = false; g_hitStreak = 0;
            } else {
                static uint8_t buf[GRID_W*GRID_H*4];
                if (captureScreen(buf)) {
                    int conf = detectEndScreen(buf);
                    g_lastConf = conf;
                    time_t now = time(nullptr);
                    bool fired = false;
                    if (conf >= g_cfg.threshold) {
                        g_lastHigh = now;
                        // leaky confirmation: climbs on hits, decays on misses, so a
                        // one-frame mid-game flash can't reach REQUIRED_STREAK but the
                        // real screen does — and a single dropped frame won't reset it.
                        if (g_hitStreak < STREAK_CAP) g_hitStreak++;
                        if (g_hitStreak >= REQUIRED_STREAK && !g_reminded) {
                            g_reminded = true; showReminder(true); fired = true;
                        }
                    } else {
                        if (g_hitStreak > 0) g_hitStreak--;
                    }
                    // Re-arm only once the end screen has actually been gone a while.
                    // A brief flicker (e.g. cursor over a button) won't re-arm, so it
                    // won't confusingly flip back to "watching" mid-screen or double-fire.
                    if (g_reminded && (now - g_lastHigh) >= REARM_SEC) g_reminded = false;
                    if (conf >= g_cfg.threshold - 20 || fired) logDetect(conf, g_hitStreak, fired);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_HOTKEY:
        switch (wp) {
        case HK_TRIGGER: showReminder(false); return 0;
        case HK_HIDE:    ShowWindow(hwnd, IsWindowVisible(hwnd) ? SW_HIDE : SW_SHOW); return 0;
        case HK_PAUSE:   cmdTogglePause(); return 0;
        case HK_QUIT:    DestroyWindow(hwnd); return 0;
        case HK_MOVE:    setMoveMode(!g_moveMode); return 0;
        case HK_SETTINGS: toggleSettings(); return 0;
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
        if (LOWORD(wp) >= IDM_GOAL_BASE && LOWORD(wp) < IDM_GOAL_BASE + 6) {
            static const int presets[6] = {25, 50, 75, 100, 150, 200};
            g_cfg.goal = presets[LOWORD(wp) - IDM_GOAL_BASE];
            saveConfig(); InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        switch (LOWORD(wp)) {
        case IDM_TOGGLE:  cmdTogglePause(); return 0;
        case IDM_TEST:    showReminder(false); return 0;
        case IDM_SETTINGS: toggleSettings(); return 0;
        case IDM_MORESENS: cmdMoreSensitive(); return 0;
        case IDM_LESSSENS: cmdLessSensitive(); return 0;
        case IDM_WORKOUTS: cmdEditWorkouts(); return 0;
        case IDM_SHOW:    ShowWindow(hwnd, IsWindowVisible(hwnd)?SW_HIDE:SW_SHOW); return 0;
        case IDM_MOVE:    setMoveMode(!g_moveMode); return 0;
        case IDM_RESETPROG: checkDayRollover(); g_cfg.progress = 0; saveConfig(); InvalidateRect(hwnd,nullptr,FALSE); return 0;
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
        UnregisterHotKey(hwnd, HK_SETTINGS);
        if (g_settings) DestroyWindow(g_settings);
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
    g_fTiny   = makeFont(12, FW_SEMIBOLD);
    g_fNum    = makeFont(16, FW_BOLD);
    g_fDone   = makeFont(20, FW_BOLD);

    loadWorkouts();
    loadConfig();
    checkDayRollover();

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

    WNDCLASSW wsc = {};
    wsc.lpfnWndProc = SettingsWndProc; wsc.hInstance = hInst;
    wsc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wsc.lpszClassName = SETTINGS_CLASS;
    RegisterClassW(&wsc);

    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    int WH = hudHeight();
    // use the saved position if we have one, else default to the top-right corner
    int wx = (g_cfg.posX >= 0) ? g_cfg.posX : sx - WIN_W - 24;
    int wy = (g_cfg.posY >= 0) ? g_cfg.posY : 24;
    if (wx < 0) wx = 0; if (wx > sx - WIN_W) wx = sx - WIN_W;   // keep on-screen
    if (wy < 0) wy = 0; if (wy > sy - WH) wy = sy - WH;
    // WS_EX_TRANSPARENT => click-through: the mouse passes straight to the game.
    g_main = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        MAIN_CLASS, L"OVRK Workout", WS_POPUP,
        wx, wy, WIN_W, WH, nullptr, nullptr, hInst, nullptr);
    if (!g_main) return 1;
    HRGN rgn = CreateRoundRectRgn(0, 0, WIN_W+1, WH+1, 16, 16);
    SetWindowRgn(g_main, rgn, TRUE);
    SetLayeredWindowAttributes(g_main, 0, 244, LWA_ALPHA);
    ShowWindow(g_main, SW_SHOW);
    UpdateWindow(g_main);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
