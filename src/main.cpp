#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#include <vector>
#include <random>
#include <algorithm>

#include "resource.h"

namespace {

// Keep combo order == enum order.
enum class Mode : int {
    AllDots_RowMajor = 0,
    AllDots_ColumnMajor = 1,
    RandomGroupings = 2,
    DashesCycle_14_25_36_78 = 3,

    Dots78 = 4,
    Dots1237 = 5,
    Dots4568 = 6,
    Alternate1237_4568 = 7,

    Dots1346 = 8,   // mask 0x2D
    Dots1256 = 9,   // mask 0x33
    Dots1267 = 10,  // mask 0x63
    Dots347  = 11,  // mask 0x4C
    Dots12367 = 12, // mask 0x67
    Dots12356 = 13, // mask 0x37
    Dots3678  = 14  // mask 0xE4
};

static std::wstring ModeLabel(Mode m);

struct AppState {
    HWND dlg = nullptr;
    HWND output = nullptr;
    HWND status = nullptr;

    // We create this dynamically from code, so you don't have to touch the .rc.
    HWND chkWholeLine = nullptr;

    // Output subclass to catch S and Esc without creating a caret.
    WNDPROC oldOutputProc = nullptr;

    bool running = false;
    bool paused = false;
    UINT_PTR timerId = 0;

    // Optional: stop hotkey while running (S).
    bool hotkeyRegistered = false;
    UINT hotkeyId = 1;

    // Settings
    int cols = 24;
    int rows = 4;
    int intervalMs = 500;
    Mode mode = Mode::AllDots_RowMajor;
    bool loop = true;
    bool wholeLine = false;

    int totalCells = 96; // cols * rows (single long line)

    // Animation state
    bool phaseOn = true;    // ON -> OFF -> advance
    int stepIndex = 0;      // 0..totalCells-1
    int dashSubStep = 0;    // 0..3 for 1-4/2-5/3-6/7-8 cycle

    std::mt19937 rng{ std::random_device{}() };
};

AppState g;

constexpr wchar_t kBrailleBlank = 0x2800;

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

static void SetStatus(const std::wstring& s) {
    if (g.status) SetWindowTextW(g.status, s.c_str());
}

static void ShowError(HWND parent, const std::wstring& msg) {
    MessageBoxW(parent, msg.c_str(), L"Braille Display Calibration Tool", MB_ICONERROR | MB_OK);
}

static bool ReadInt(HWND dlg, int id, int& outValue) {
    BOOL ok = FALSE;
    UINT v = GetDlgItemInt(dlg, id, &ok, FALSE);
    if (!ok) return false;
    outValue = (int)v;
    return true;
}

static std::wstring FormatCounts(int cols, int rows) {
    long long cells = 1LL * cols * rows;
    long long dots = cells * 8LL;

    std::wstring s;
    s.reserve(120);
    s += L"Cells: ";
    s += std::to_wstring(cols);
    s += L" x ";
    s += std::to_wstring(rows);
    s += L" = ";
    s += std::to_wstring(cells);
    s += L". Dots: ";
    s += std::to_wstring(cells);
    s += L" x 8 = ";
    s += std::to_wstring(dots);
    s += L".";
    return s;
}

static void EnableRunningUi(HWND dlg, bool running) {
    EnableWindow(GetDlgItem(dlg, IDC_START), running ? FALSE : TRUE);
    EnableWindow(GetDlgItem(dlg, IDC_STOP),  running ? TRUE : FALSE);

    EnableWindow(GetDlgItem(dlg, IDC_COLUMNS),  running ? FALSE : TRUE);
    EnableWindow(GetDlgItem(dlg, IDC_ROWS),     running ? FALSE : TRUE);
    EnableWindow(GetDlgItem(dlg, IDC_INTERVAL), running ? FALSE : TRUE);
    EnableWindow(GetDlgItem(dlg, IDC_MODE),     running ? FALSE : TRUE);
    EnableWindow(GetDlgItem(dlg, IDC_LOOP),     running ? FALSE : TRUE);

    if (g.chkWholeLine) {
    EnableWindow(GetDlgItem(dlg, IDC_WHOLELINE), running ? FALSE : TRUE);
    }
}

static void NotifyOutputChanged(HWND hwnd) {
    // Encourage screen readers to notice updates.
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, hwnd, OBJID_CLIENT, CHILDID_SELF);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, hwnd, OBJID_CLIENT, CHILDID_SELF);
}

static void SetOutputText(const std::wstring& s) {
    if (!g.output) return;
    SetWindowTextW(g.output, s.c_str());
    NotifyOutputChanged(g.output);
}

static wchar_t MaskToBrailleCell(unsigned char mask) {
    return (wchar_t)(0x2800 + (wchar_t)mask);
}

static bool IsColumnMajorMode(Mode m) {
    return m == Mode::AllDots_ColumnMajor;
}

static bool IsRandomMode(Mode m) {
    return m == Mode::RandomGroupings;
}

static bool IsDashCycleMode(Mode m) {
    return m == Mode::DashesCycle_14_25_36_78;
}

static bool IsAlternateMode(Mode m) {
    return m == Mode::Alternate1237_4568;
}

static unsigned char FixedMaskForMode(Mode m) {
    switch (m) {
    case Mode::AllDots_RowMajor:
    case Mode::AllDots_ColumnMajor:
        return 0xFF;

    case Mode::Dots78:   return 0xC0;
    case Mode::Dots1237: return 0x47;
    case Mode::Dots4568: return 0xB8;

    case Mode::Dots1346:  return 0x2D;
    case Mode::Dots1256:  return 0x33;
    case Mode::Dots1267:  return 0x63;
    case Mode::Dots347:   return 0x4C;
    case Mode::Dots12367: return 0x67;
    case Mode::Dots12356: return 0x37;
    case Mode::Dots3678:  return 0xE4;

    default:
        return 0x00;
    }
}

static wchar_t DashCycleCell(int subStep) {
    // Cycle: dots 1-4, 2-5, 3-6, 7-8
    // bit 0..7 == dot 1..8
    static const unsigned char masks[4] = {
        0x09, // 1 + 4
        0x12, // 2 + 5
        0x24, // 3 + 6
        0xC0  // 7 + 8
    };
    return MaskToBrailleCell(masks[subStep & 3]);
}

static std::wstring BuildBlankLine() {
    return std::wstring((size_t)g.totalCells, kBrailleBlank);
}

static int MapStepToCellIndex(int stepIndex) {
    if (!IsColumnMajorMode(g.mode)) return stepIndex;

    // Column-major order over a virtual grid:
    // for col in 0..cols-1:
    //   for row in 0..rows-1:
    //      index = row*cols + col
    int col = stepIndex / g.rows;
    int row = stepIndex % g.rows;

    if (col < 0) col = 0;
    if (col >= g.cols) col = g.cols - 1;
    if (row < 0) row = 0;
    if (row >= g.rows) row = g.rows - 1;

    return row * g.cols + col;
}

static std::wstring BuildLineForTick() {
    std::wstring line = BuildBlankLine();
    if (g.totalCells <= 0) return line;

    // Random mode is special:
    if (IsRandomMode(g.mode)) {
        // If "Blink whole line" is checked, we treat it literally:
        // ON phase: every cell gets a random non-zero mask
        // OFF phase: blank line
        if (g.wholeLine) {
            if (!g.phaseOn) return line;
            std::uniform_int_distribution<int> dist(1, 255);
            for (int i = 0; i < g.totalCells; ++i) {
                unsigned char mask = (unsigned char)dist(g.rng);
                line[(size_t)i] = MaskToBrailleCell(mask);
            }
            return line;
        }

        // Otherwise, "groupings": sprinkle random patterns across the line, no forced blank phase.
        std::uniform_real_distribution<double> chance(0.0, 1.0);
        std::uniform_int_distribution<int> dist(1, 255);

        const double fillProb = 0.35;
        for (int i = 0; i < g.totalCells; ++i) {
            if (chance(g.rng) <= fillProb) {
                unsigned char mask = (unsigned char)dist(g.rng);
                line[(size_t)i] = MaskToBrailleCell(mask);
            }
        }
        return line;
    }

    // Whole-line blink mode (applies to every non-random mode)
    if (g.wholeLine) {
        if (!g.phaseOn) return line;

        if (IsDashCycleMode(g.mode)) {
            const wchar_t cell = DashCycleCell(g.dashSubStep);
            std::fill(line.begin(), line.end(), cell);
            return line;
        }

        if (IsAlternateMode(g.mode)) {
            const wchar_t a = MaskToBrailleCell(0x47); // 1237
            const wchar_t b = MaskToBrailleCell(0xB8); // 4568
            for (size_t i = 0; i < line.size(); ++i) {
                line[i] = (i % 2 == 0) ? a : b;
            }
            return line;
        }

        // Fixed mask
        unsigned char mask = FixedMaskForMode(g.mode);
        if (mask == 0x00) mask = 0xFF;
        std::fill(line.begin(), line.end(), MaskToBrailleCell(mask));
        return line;
    }

    // Walking mode (default): one active cell blinks at a time.
    const int cellIndex = MapStepToCellIndex(g.stepIndex);
    if (cellIndex < 0 || cellIndex >= g.totalCells) return line;

    if (!g.phaseOn) {
        return line; // OFF phase: blank line
    }

    if (IsDashCycleMode(g.mode)) {
        line[(size_t)cellIndex] = DashCycleCell(g.dashSubStep);
        return line;
    }

    if (IsAlternateMode(g.mode)) {
        // Alternate pattern based on *actual* cell index parity.
        const unsigned char mask = ((cellIndex % 2) == 0) ? 0x47 : 0xB8;
        line[(size_t)cellIndex] = MaskToBrailleCell(mask);
        return line;
    }

    unsigned char mask = FixedMaskForMode(g.mode);
    if (mask == 0x00) mask = 0xFF;
    line[(size_t)cellIndex] = MaskToBrailleCell(mask);
    return line;
}

static void UnregisterStopHotkey(HWND dlg) {
    if (!g.hotkeyRegistered) return;
    UnregisterHotKey(dlg, g.hotkeyId);
    g.hotkeyRegistered = false;
}

static void RegisterStopHotkey(HWND dlg) {
    if (g.hotkeyRegistered) return;

    // While running, config fields are disabled, so grabbing S is safe.
    if (RegisterHotKey(dlg, g.hotkeyId, MOD_NOREPEAT, 'S')) {
        g.hotkeyRegistered = true;
    }
}

static void StopCalibration(HWND dlg) {
    if (!g.running) return;

    if (g.timerId) {
        KillTimer(dlg, g.timerId);
        g.timerId = 0;
    }

    UnregisterStopHotkey(dlg);

    g.running = false;
    g.paused = false;
    EnableRunningUi(dlg, false);

    // Blank output
    SetOutputText(BuildBlankLine());

    // Put focus back into the main control list
    HWND modeCombo = GetDlgItem(dlg, IDC_MODE);
    if (modeCombo) SetFocus(modeCombo);

    SetStatus(L"Status: Idle. (Esc exits when idle. While running: P/Enter pauses; Esc or S stops.)");
}


static void TogglePause(HWND dlg) {
    if (!g.running) return;

    if (!g.paused) {
        // Pause
        g.paused = true;

        if (g.timerId) {
            KillTimer(dlg, g.timerId);
            g.timerId = 0;
        }

        std::wstring status = L"Status: Paused. ";
        status += ModeLabel(g.mode);
        status += L". ";
        status += FormatCounts(g.cols, g.rows);
        status += L" Resume: P or Enter. Stop: Esc or S.";
        SetStatus(status);
    } else {
        // Resume
        g.paused = false;

        if (!g.timerId) {
            g.timerId = SetTimer(dlg, 1, (UINT)g.intervalMs, nullptr);
            if (!g.timerId) {
                ShowError(dlg, L"Failed to resume timer.");
                StopCalibration(dlg);
                return;
            }
        }

        // Keep focus on the output area so key controls work consistently.
        if (g.output) SetFocus(g.output);

        std::wstring status = L"Status: Running. ";
        status += ModeLabel(g.mode);
        status += L". ";
        status += FormatCounts(g.cols, g.rows);
        status += L" Interval: ";
        status += std::to_wstring(g.intervalMs);
        status += L" ms. ";
        status += g.wholeLine ? L"Blink whole line: ON. " : L"Blink whole line: OFF (walking). ";
        status += L"Pause: P or Enter. Stop: Esc or S.";
        SetStatus(status);
    }
}

static void AdvanceState(HWND dlg) {
    // Random groupings (non-whole-line) just keeps updating; no on/off stepping.
    if (IsRandomMode(g.mode) && !g.wholeLine) return;

    // For all other situations, we blink ON/OFF.
    if (g.phaseOn) {
        g.phaseOn = false;
        return;
    }

    // OFF -> ON (this is where we advance the walk/cycle)
    g.phaseOn = true;

    if (g.wholeLine) {
        // Whole-line: there is no walk. Only dashes has an internal cycle worth advancing.
        if (IsDashCycleMode(g.mode)) {
            g.dashSubStep++;
            if (g.dashSubStep >= 4) {
                g.dashSubStep = 0;
                if (!g.loop) {
                    StopCalibration(dlg);
                }
            }
        } else {
            // For whole-line blink, if loop is off, one blink cycle is enough.
            if (!g.loop) {
                StopCalibration(dlg);
            }
        }
        return;
    }

    // Walking mode: advance cell position (and dash substep if needed).
    if (IsDashCycleMode(g.mode)) {
        g.dashSubStep++;
        if (g.dashSubStep >= 4) {
            g.dashSubStep = 0;
            g.stepIndex++;
        }
    } else {
        g.stepIndex++;
    }

    if (g.stepIndex >= g.totalCells) {
        if (g.loop) {
            g.stepIndex = 0;
        } else {
            StopCalibration(dlg);
        }
    }
}

static bool ReadSettingsFromDialog(HWND dlg) {
    int cols = 0, rows = 0, intervalMs = 0;

    if (!ReadInt(dlg, IDC_COLUMNS, cols) || cols <= 0) {
        ShowError(dlg, L"Columns must be a positive number.");
        return false;
    }
    if (!ReadInt(dlg, IDC_ROWS, rows) || rows <= 0) {
        ShowError(dlg, L"Rows must be a positive number.");
        return false;
    }
    if (!ReadInt(dlg, IDC_INTERVAL, intervalMs) || intervalMs <= 0) {
        ShowError(dlg, L"Interval must be a positive number of milliseconds.");
        return false;
    }

    long long totalCells = 1LL * cols * rows;
    if (totalCells <= 0 || totalCells > 5000) {
        ShowError(dlg, L"Total cells is too large. Try smaller values (typical is 96 or 300).");
        return false;
    }

    HWND hMode = GetDlgItem(dlg, IDC_MODE);
    int sel = (hMode ? (int)SendMessageW(hMode, CB_GETCURSEL, 0, 0) : 0);
    if (sel < 0) sel = 0;

    g.cols = cols;
    g.rows = rows;
    g.intervalMs = intervalMs;
    g.loop = (IsDlgButtonChecked(dlg, IDC_LOOP) == BST_CHECKED);
    g.mode = (Mode)sel;
    g.totalCells = (int)totalCells;

    if (g.chkWholeLine) {

    g.wholeLine = (IsDlgButtonChecked(dlg, IDC_WHOLELINE) == BST_CHECKED);
    } else {
        g.wholeLine = false;
    }

    return true;
}

static std::wstring ModeLabel(Mode m) {
    switch (m) {
    case Mode::AllDots_RowMajor: return L"All dots (1-8), row-major walk";
    case Mode::AllDots_ColumnMajor: return L"All dots (1-8), column-major walk";
    case Mode::RandomGroupings: return L"Random dot groupings";
    case Mode::DashesCycle_14_25_36_78: return L"Dashes cycle (1-4 / 2-5 / 3-6 / 7-8)";

    case Mode::Dots78: return L"Dots 7-8";
    case Mode::Dots1237: return L"Dots 1-2-3-7";
    case Mode::Dots4568: return L"Dots 4-5-6-8";
    case Mode::Alternate1237_4568: return L"Alternating 1237 / 4568";

    case Mode::Dots1346: return L"Dots 1-3-4-6";
    case Mode::Dots1256: return L"Dots 1-2-5-6";
    case Mode::Dots1267: return L"Dots 1-2-6-7";
    case Mode::Dots347:  return L"Dots 3-4-7";
    case Mode::Dots12367:return L"Dots 1-2-3-6-7";
    case Mode::Dots12356:return L"Dots 1-2-3-5-6";
    case Mode::Dots3678: return L"Dots 3-6-7-8";
    default: return L"(unknown)";
    }
}

// Output control: focusable static, no caret.
// While running: S stops; Esc stops.
static LRESULT CALLBACK OutputProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_GETDLGCODE:
        // We only want character keys while running so S works.
        return g.running ? (DLGC_WANTCHARS | DLGC_WANTMESSAGE) : 0;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;

    case WM_KEYDOWN:
        if (g.running && wParam == VK_ESCAPE) {
            PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
            return 0;
        }
        break;

    case WM_CHAR:
        if (g.running) {
            wchar_t ch = (wchar_t)wParam;

            if (ch == L's' || ch == L'S') {
                PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_STOP, 0), 0);
                return 0;
            }

            // Pause/resume (toggle)
            if (ch == L'p' || ch == L'P' || ch == L'\r') {
                TogglePause(GetParent(hwnd));
                return 0;
            }
        }
        break;

    default:
        break;
    }

    return g.oldOutputProc
        ? CallWindowProcW(g.oldOutputProc, hwnd, msg, wParam, lParam)
        : DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ReplaceOutputEditWithStatic(HWND dlg) {
    HWND old = GetDlgItem(dlg, IDC_OUTPUT);
    if (!old) return;

    RECT r{};
    GetWindowRect(old, &r);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&r), 2);

    HFONT dlgFont = (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0);

    DestroyWindow(old);

    HWND out = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_NOTIFY | SS_LEFTNOWORDWRAP,
        r.left, r.top, r.right - r.left, r.bottom - r.top,
        dlg,
        (HMENU)IDC_OUTPUT,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (out && dlgFont) {
        SendMessageW(out, WM_SETFONT, (WPARAM)dlgFont, TRUE);
    }

    g.output = out;

    if (g.output) {
        g.oldOutputProc = (WNDPROC)SetWindowLongPtrW(g.output, GWLP_WNDPROC, (LONG_PTR)OutputProc);
    }
}

static void CreateWholeLineCheckbox(HWND dlg) {
    if (g.chkWholeLine) return;

    // If the checkbox exists in the dialog resource, use it (don't create a duplicate).
    HWND existing = GetDlgItem(dlg, IDC_WHOLELINE);
    if (existing) {
        g.chkWholeLine = existing;
        return;
    }

    HWND loop = GetDlgItem(dlg, IDC_LOOP);

    // Fallback position if we can't locate the Loop checkbox.
    int x = 80, y = 90, w = 140, h = 14;

    if (loop) {
        RECT r{};
        GetWindowRect(loop, &r);
        MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&r), 2);

        x = r.right + 10;
        y = r.top;
        h = (r.bottom - r.top);
        w = 140;
    }

    g.chkWholeLine = CreateWindowExW(
        0,
        L"BUTTON",
        L"Blink whole line",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        x, y, w, h,
        dlg,
        (HMENU)IDC_WHOLELINE, // matches resource.h if present
        GetModuleHandleW(nullptr),
        nullptr
    );

    HFONT dlgFont = (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0);
    if (g.chkWholeLine && dlgFont) {
        SendMessageW(g.chkWholeLine, WM_SETFONT, (WPARAM)dlgFont, TRUE);
    }

    // Default unchecked
    if (g.chkWholeLine) {
        SendMessageW(g.chkWholeLine, BM_SETCHECK, BST_UNCHECKED, 0);
    }
}

static void StartCalibration(HWND dlg) {
    if (g.running) return;
    if (!ReadSettingsFromDialog(dlg)) return;

    g.phaseOn = true;
    g.stepIndex = 0;
    g.dashSubStep = 0;
    g.paused = false;

    // Focus output so braille tends to follow it.
    if (g.output) SetFocus(g.output);

    // First frame immediately
    SetOutputText(BuildLineForTick());

    g.timerId = SetTimer(dlg, 1, (UINT)g.intervalMs, nullptr);
    if (!g.timerId) {
        ShowError(dlg, L"Failed to start timer.");
        StopCalibration(dlg);
        return;
    }

    g.running = true;
    EnableRunningUi(dlg, true);
    RegisterStopHotkey(dlg);

    std::wstring status = L"Status: Running. ";
    status += ModeLabel(g.mode);
    status += L". ";
    status += FormatCounts(g.cols, g.rows);
    status += L" Interval: ";
    status += std::to_wstring(g.intervalMs);
    status += L" ms. ";

    status += g.wholeLine ? L"Blink whole line: ON. " : L"Blink whole line: OFF (walking). ";
    status += L"Pause: P or Enter. Stop: Esc or S.";

    SetStatus(status);
}

INT_PTR CALLBACK MainDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        g.dlg = dlg;
        g.status = GetDlgItem(dlg, IDC_STATUS);

        ReplaceOutputEditWithStatic(dlg);
    CreateWholeLineCheckbox(dlg); 
        // Defaults
        SetDlgItemInt(dlg, IDC_COLUMNS, g.cols, FALSE);
        SetDlgItemInt(dlg, IDC_ROWS, g.rows, FALSE);
        SetDlgItemInt(dlg, IDC_INTERVAL, g.intervalMs, FALSE);
        CheckDlgButton(dlg, IDC_LOOP, BST_CHECKED);

        // Populate mode list (no "whole line" items anymore)
        HWND hMode = GetDlgItem(dlg, IDC_MODE);
        if (hMode) {
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"All dots (1-8) - row-major walk");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"All dots (1-8) - column-major walk");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Random dot groupings");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dashes cycle: 1-4 / 2-5 / 3-6 / 7-8");

            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dots 7-8");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dots 1-2-3-7");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dots 4-5-6-8");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Alternating 1237 / 4568");

            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dots 1-3-4-6");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dots 1-2-5-6");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dots 1-2-6-7");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dots 3-4-7");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dots 1-2-3-6-7");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dots 1-2-3-5-6");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"Dots 3-6-7-8");

            SendMessageW(hMode, CB_SETCURSEL, 0, 0);
        }

        EnableRunningUi(dlg, false);

        g.totalCells = g.cols * g.rows;
        SetOutputText(BuildBlankLine());

        SetStatus(L"Status: Idle. Tip: set translation to 8-dot Computer Braille. While running: P or Enter pauses; Esc or S stops.");
        return TRUE;
    }

    case WM_TIMER:
        if (wParam == 1 && g.running) {
            if (g.paused) return TRUE;
            SetOutputText(BuildLineForTick());
            AdvanceState(dlg);
            return TRUE;
        }
        return FALSE;

    case WM_HOTKEY:
        if (g.running && (UINT)wParam == g.hotkeyId) {
            StopCalibration(dlg);
            return TRUE;
        }
        return FALSE;

    case WM_COMMAND: {
        const int id = LOWORD(wParam);

        switch (id) {
        case IDC_START:
            StartCalibration(dlg);
            return TRUE;

        case IDC_STOP:
            StopCalibration(dlg);
            return TRUE;

        case IDCANCEL:
            // Esc while running should STOP, not exit.
            if (g.running) {
                StopCalibration(dlg);
                return TRUE;
            }
            EndDialog(dlg, 0);
            return TRUE;

        default:
            return FALSE;
        }
    }

    case WM_CLOSE:
        if (g.running) {
            StopCalibration(dlg);
            return TRUE;
        }
        EndDialog(dlg, 0);
        return TRUE;

    default:
        return FALSE;
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    return (int)DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDlgProc, 0);
}
