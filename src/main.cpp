#include <windows.h>
#include <windowsx.h>
#include <magnification.h>

#include <algorithm>

static constexpr wchar_t kControllerClass[] = L"ScreenInvertFilter.Controller";
static constexpr wchar_t kOverlayClass[] = L"ScreenInvertFilter.Overlay";
static constexpr wchar_t kSelectClass[] = L"ScreenInvertFilter.Select";

static constexpr UINT_PTR kTimerRedraw = 1;

static constexpr int kBtnToggleId = 1001;
static constexpr int kBtnSelectId = 1002;

struct VirtualScreen {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct AppState {
  HINSTANCE instance = nullptr;
  HWND controller = nullptr;
  HWND overlay = nullptr;
  HWND magnifier = nullptr;
  HWND selector = nullptr;

  HWND toggleBtn = nullptr;
  HWND selectBtn = nullptr;

  bool enabled = false;
  bool hasSelection = false;
  RECT selection{};

  bool isDragging = false;
  POINT dragStart{};
  POINT dragCurrent{};

  VirtualScreen vscreen{};
  UINT refreshIntervalMs = 33;
};

static AppState gState;

static UINT ClampRefreshIntervalMs(UINT intervalMs) {
  intervalMs = std::max<UINT>(1, intervalMs);
  intervalMs = std::min<UINT>(33, intervalMs);
  if (intervalMs < 4) intervalMs = 4;
  return intervalMs;
}

static UINT GetSelectionMonitorRefreshHz() {
  if (!gState.hasSelection) return 0;
  const LONG cx = gState.selection.left + (gState.selection.right - gState.selection.left) / 2;
  const LONG cy = gState.selection.top + (gState.selection.bottom - gState.selection.top) / 2;

  const HMONITOR mon = MonitorFromPoint(POINT{ cx, cy }, MONITOR_DEFAULTTONEAREST);
  if (!mon) return 0;

  MONITORINFOEXW mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(mon, &mi)) return 0;

  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  if (!EnumDisplaySettingsExW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm, 0)) return 0;

  if (dm.dmDisplayFrequency <= 1 || dm.dmDisplayFrequency > 1000) return 0;
  return static_cast<UINT>(dm.dmDisplayFrequency);
}

static UINT ComputeDesiredRefreshIntervalMs() {
  UINT hz = GetSelectionMonitorRefreshHz();
  if (hz == 0) hz = 120;
  const UINT interval = (1000u + (hz / 2u)) / hz;
  return ClampRefreshIntervalMs(interval);
}

static void UpdateOverlayRefreshTimer(HWND overlayHwnd) {
  if (!overlayHwnd) return;
  if (!gState.enabled || !gState.hasSelection) {
    KillTimer(overlayHwnd, kTimerRedraw);
    return;
  }

  const UINT desired = ComputeDesiredRefreshIntervalMs();
  if (gState.refreshIntervalMs != desired) {
    KillTimer(overlayHwnd, kTimerRedraw);
    gState.refreshIntervalMs = desired;
  }
  SetTimer(overlayHwnd, kTimerRedraw, gState.refreshIntervalMs, nullptr);
}

static VirtualScreen GetVirtualScreen() {
  VirtualScreen vs;
  vs.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
  vs.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  vs.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  vs.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  return vs;
}

static RECT NormalizeRect(POINT a, POINT b) {
  RECT r{};
  r.left = std::min(a.x, b.x);
  r.top = std::min(a.y, b.y);
  r.right = std::max(a.x, b.x);
  r.bottom = std::max(a.y, b.y);
  return r;
}

static int RectWidth(const RECT& r) { return r.right - r.left; }
static int RectHeight(const RECT& r) { return r.bottom - r.top; }

static void TryEnablePerMonitorV2DpiAwareness() {
  const HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (!user32) return;
  using Fn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
  const auto fn = reinterpret_cast<Fn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
  if (!fn) return;
  fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

static MAGCOLOREFFECT InvertEffect() {
  MAGCOLOREFFECT fx{};
  fx.transform[0][0] = -1.0f;
  fx.transform[1][1] = -1.0f;
  fx.transform[2][2] = -1.0f;
  fx.transform[3][3] = 1.0f;
  fx.transform[4][0] = 1.0f;
  fx.transform[4][1] = 1.0f;
  fx.transform[4][2] = 1.0f;
  fx.transform[4][4] = 1.0f;
  return fx;
}

static void UpdateMagnifierConfig() {
  if (!gState.magnifier || !gState.hasSelection) return;

  MagSetWindowSource(gState.magnifier, gState.selection);

  MAGTRANSFORM t{};
  t.v[0][0] = 1.0f;
  t.v[1][1] = 1.0f;
  t.v[2][2] = 1.0f;
  MagSetWindowTransform(gState.magnifier, &t);

  auto fx = InvertEffect();
  MagSetColorEffect(gState.magnifier, &fx);

  HWND exclude[4]{};
  int count = 0;
  if (gState.overlay) exclude[count++] = gState.overlay;
  if (gState.controller) exclude[count++] = gState.controller;
  if (gState.selector) exclude[count++] = gState.selector;
  if (count > 0) {
    MagSetWindowFilterList(gState.magnifier, MW_FILTERMODE_EXCLUDE, count, exclude);
  }
}

static void EnsureOverlay() {
  if (!gState.hasSelection) return;

  const int w = RectWidth(gState.selection);
  const int h = RectHeight(gState.selection);
  if (w <= 0 || h <= 0) return;

  if (!gState.overlay) {
    gState.overlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kOverlayClass, L"",
        WS_POPUP,
        gState.selection.left, gState.selection.top, w, h,
        nullptr, nullptr, gState.instance, nullptr);

    SetLayeredWindowAttributes(gState.overlay, 0, 255, LWA_ALPHA);
    ShowWindow(gState.overlay, gState.enabled ? SW_SHOWNOACTIVATE : SW_HIDE);
    UpdateWindow(gState.overlay);
  } else {
    MoveWindow(gState.overlay, gState.selection.left, gState.selection.top, w, h, TRUE);
    ShowWindow(gState.overlay, gState.enabled ? SW_SHOWNOACTIVATE : SW_HIDE);
  }

  if (gState.overlay && gState.magnifier) {
    SetWindowPos(gState.magnifier, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  UpdateMagnifierConfig();
  UpdateOverlayRefreshTimer(gState.overlay);
}

static void StartSelection() {
  if (gState.selector) return;
  gState.vscreen = GetVirtualScreen();

  gState.selector = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
      kSelectClass, L"",
      WS_POPUP,
      gState.vscreen.x, gState.vscreen.y, gState.vscreen.w, gState.vscreen.h,
      nullptr, nullptr, gState.instance, nullptr);

  SetLayeredWindowAttributes(gState.selector, 0, 80, LWA_ALPHA);
  ShowWindow(gState.selector, SW_SHOW);
  SetFocus(gState.selector);
  SetCapture(gState.selector);
  gState.isDragging = false;
  UpdateMagnifierConfig();
}

static void EndSelection(bool commit) {
  if (!gState.selector) return;

  ReleaseCapture();

  if (commit && gState.isDragging) {
    RECT r = NormalizeRect(gState.dragStart, gState.dragCurrent);
    if (RectWidth(r) >= 8 && RectHeight(r) >= 8) {
      gState.selection = r;
      gState.hasSelection = true;
      gState.enabled = true;
    }
  }

  DestroyWindow(gState.selector);
  gState.selector = nullptr;
  gState.isDragging = false;

  EnsureOverlay();
  UpdateMagnifierConfig();
}

static void UpdateControllerUi() {
  if (!gState.toggleBtn) return;
  const wchar_t* text = gState.enabled ? L"关闭滤镜" : L"开启滤镜";
  SetWindowTextW(gState.toggleBtn, text);
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_CREATE: {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      gState.magnifier = CreateWindowExW(
          WS_EX_TRANSPARENT,
          WC_MAGNIFIER, L"",
          WS_CHILD | WS_VISIBLE,
          0, 0, rc.right - rc.left, rc.bottom - rc.top,
          hwnd, nullptr, gState.instance, nullptr);
      UpdateOverlayRefreshTimer(hwnd);
      UpdateMagnifierConfig();
      return 0;
    }
    case WM_TIMER: {
      if (wparam != kTimerRedraw) break;
      if (!gState.enabled || !gState.hasSelection || !gState.magnifier) return 0;
      MagSetWindowSource(gState.magnifier, gState.selection);
      InvalidateRect(gState.magnifier, nullptr, FALSE);
      UpdateWindow(gState.magnifier);
      return 0;
    }
    case WM_SIZE: {
      if (gState.magnifier) {
        const int w = LOWORD(lparam);
        const int h = HIWORD(lparam);
        SetWindowPos(gState.magnifier, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return 0;
    }
    case WM_DESTROY:
      KillTimer(hwnd, kTimerRedraw);
      if (gState.magnifier && GetParent(gState.magnifier) == hwnd) {
        gState.magnifier = nullptr;
      }
      gState.overlay = nullptr;
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void DrawSelectionRect(HDC dc, const RECT& r) {
  const HBRUSH nullBrush = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
  const HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 80, 80));
  const HGDIOBJ oldBrush = SelectObject(dc, nullBrush);
  const HGDIOBJ oldPen = SelectObject(dc, pen);
  Rectangle(dc, r.left, r.top, r.right, r.bottom);
  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  DeleteObject(pen);
}

static LRESULT CALLBACK SelectProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_KEYDOWN:
      if (wparam == VK_ESCAPE) {
        EndSelection(false);
        return 0;
      }
      break;
    case WM_LBUTTONDOWN: {
      SetFocus(hwnd);
      const int x = GET_X_LPARAM(lparam) + gState.vscreen.x;
      const int y = GET_Y_LPARAM(lparam) + gState.vscreen.y;
      gState.dragStart = POINT{ x, y };
      gState.dragCurrent = gState.dragStart;
      gState.isDragging = true;
      InvalidateRect(hwnd, nullptr, TRUE);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (!gState.isDragging) return 0;
      const int x = GET_X_LPARAM(lparam) + gState.vscreen.x;
      const int y = GET_Y_LPARAM(lparam) + gState.vscreen.y;
      gState.dragCurrent = POINT{ x, y };
      InvalidateRect(hwnd, nullptr, TRUE);
      return 0;
    }
    case WM_LBUTTONUP:
      EndSelection(true);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      SetBkMode(dc, TRANSPARENT);
      if (gState.isDragging) {
        RECT sr = NormalizeRect(gState.dragStart, gState.dragCurrent);
        RECT wr{};
        wr.left = sr.left - gState.vscreen.x;
        wr.top = sr.top - gState.vscreen.y;
        wr.right = sr.right - gState.vscreen.x;
        wr.bottom = sr.bottom - gState.vscreen.y;
        DrawSelectionRect(dc, wr);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_DESTROY:
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK ControllerProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_CREATE: {
      gState.toggleBtn = CreateWindowExW(
          0, L"BUTTON", L"开启滤镜",
          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
          12, 12, 140, 34,
          hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnToggleId)), gState.instance, nullptr);

      gState.selectBtn = CreateWindowExW(
          0, L"BUTTON", L"选择区域",
          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
          12, 54, 140, 34,
          hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnSelectId)), gState.instance, nullptr);

      UpdateControllerUi();
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wparam);
      if (id == kBtnToggleId) {
        if (!gState.hasSelection) {
          StartSelection();
        } else {
          gState.enabled = !gState.enabled;
          EnsureOverlay();
        }
        UpdateControllerUi();
        return 0;
      }
      if (id == kBtnSelectId) {
        StartSelection();
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static bool RegisterClasses(HINSTANCE instance) {
  WNDCLASSW wc{};

  wc = WNDCLASSW{};
  wc.lpfnWndProc = ControllerProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kControllerClass;
  if (!RegisterClassW(&wc)) return false;

  wc = WNDCLASSW{};
  wc.lpfnWndProc = OverlayProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kOverlayClass;
  if (!RegisterClassW(&wc)) return false;

  wc = WNDCLASSW{};
  wc.lpfnWndProc = SelectProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kSelectClass;
  if (!RegisterClassW(&wc)) return false;

  return true;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  TryEnablePerMonitorV2DpiAwareness();

  gState.instance = instance;
  gState.vscreen = GetVirtualScreen();

  if (!MagInitialize()) return 1;
  if (!RegisterClasses(instance)) {
    MagUninitialize();
    return 1;
  }

  gState.controller = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
      kControllerClass, L"屏幕色反滤镜",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
      gState.vscreen.x + 32, gState.vscreen.y + 32, 176, 140,
      nullptr, nullptr, instance, nullptr);

  if (!gState.controller) {
    MagUninitialize();
    return 1;
  }

  ShowWindow(gState.controller, SW_SHOW);
  UpdateWindow(gState.controller);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (gState.overlay) DestroyWindow(gState.overlay);
  if (gState.selector) DestroyWindow(gState.selector);
  MagUninitialize();
  return 0;
}
