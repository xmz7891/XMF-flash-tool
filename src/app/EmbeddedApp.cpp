#include "EmbeddedApp.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winuser.h>
#include <tlhelp32.h>

namespace {
bool isProcessInTree(DWORD pid, DWORD root)
{
    if (pid == root)
        return true;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD current = pid;
    bool found = false;
    for (int depth = 0; depth < 16 && current && !found; ++depth) {
        if (current == root) {
            found = true;
            break;
        }
        DWORD parent = 0;
        if (!Process32FirstW(snapshot, &entry))
            break;
        do {
            if (entry.th32ProcessID == current) {
                parent = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
        current = parent;
    }
    CloseHandle(snapshot);
    return found;
}

struct WindowSearch {
    DWORD pid = 0;
    HWND best = nullptr;
    int bestScore = 0;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM param)
{
    auto* search = reinterpret_cast<WindowSearch*>(param);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (!isProcessInTree(windowPid, search->pid))
        return TRUE;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;
    wchar_t className[64] = {0};
    GetClassNameW(hwnd, className, 63);
    if (wcscmp(className, L"#32770") == 0)
        return TRUE;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_DLGMODALFRAME)
        return TRUE;

    wchar_t titleBuf[256] = {0};
    GetWindowTextW(hwnd, titleBuf, 255);
    wchar_t classBuf[128] = {0};
    GetClassNameW(hwnd, classBuf, 127);
    const QString titleText = QString::fromWCharArray(titleBuf);
    const QString classText = QString::fromWCharArray(classBuf);
    // .NET / WinForms tools create invisible helper windows that carry a window
    // title before their real form appears; never treat those as the main window.
    if (titleText.contains(QStringLiteral("BroadcastEventWindow"), Qt::CaseInsensitive)
        || titleText.contains(QStringLiteral("GDI+ Hook"), Qt::CaseInsensitive)
        || classText.contains(QStringLiteral("GDI+ Hook"), Qt::CaseInsensitive)
        || classText.contains(QStringLiteral("IME"), Qt::CaseInsensitive)
        || titleText == QLatin1String("Default IME"))
        return TRUE;
    if (!IsWindowVisible(hwnd))
        return TRUE;
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect))
        return TRUE;
    const int area = (rect.right - rect.left) * (rect.bottom - rect.top);
    const bool visible = IsWindowVisible(hwnd) != 0;
    wchar_t title[256] = {0};
    GetWindowTextW(hwnd, title, 255);
    const int score = area + (visible ? 100000000 : 0) + (title[0] ? 50000000 : 0);
    if (score > search->bestScore) {
        search->bestScore = score;
        search->best = hwnd;
    }
    return TRUE;
}

qint64 findProcessByExe(const QString& exePath)
{
    // Debug and packaged builds can hold the same vendor tool under different
    // data roots, so match by executable name instead of the full path.
    const QString wanted = QFileInfo(exePath).fileName().toLower();
    if (wanted.isEmpty())
        return 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    qint64 found = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == GetCurrentProcessId())
                continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process)
                continue;
            wchar_t path[1024] = {0};
            DWORD size = 1024;
            if (QueryFullProcessImageNameW(process, 0, path, &size)) {
                const QString candidate = QFileInfo(QString::fromWCharArray(path)).fileName().toLower();
                if (candidate == wanted)
                    found = qint64(entry.th32ProcessID);
            }
            CloseHandle(process);
            if (found)
                break;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

HWND findTopWindow(DWORD pid)
{
    WindowSearch search;
    search.pid = pid;
    EnumWindows(&enumWindowsProc, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

struct LaunchResult {
    qint64 pid = 0;
    bool ok = false;
};

LaunchResult launchHidden(const QString& exe, const QStringList& arguments, const QString& workDir)
{
    LaunchResult result;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWMINNOACTIVE;
    PROCESS_INFORMATION process{};

    QString commandLine = QLatin1Char('"') + QDir::toNativeSeparators(exe) + QLatin1Char('"');
    for (const QString& argument : arguments) {
        commandLine += QLatin1Char(' ');
        if (argument.contains(QLatin1Char(' ')))
            commandLine += QLatin1Char('"') + argument + QLatin1Char('"');
        else
            commandLine += argument;
    }
    std::wstring mutableCommand = commandLine.toStdWString();
    const QString nativeWorkDir = QDir::toNativeSeparators(workDir);
    const BOOL created = CreateProcessW(
        nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
        nativeWorkDir.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(nativeWorkDir.utf16()), &startup,
        &process);
    if (!created)
        return result;
    result.ok = true;
    result.pid = qint64(process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}
} // namespace
#endif

namespace gf {

EmbeddedApp::EmbeddedApp(QObject* parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(60);
    connect(&m_pollTimer, &QTimer::timeout, this, [this]() {
#ifdef Q_OS_WIN
        if (!m_pid)
            return;
        const HWND hwnd = findTopWindow(DWORD(m_pid));
        if (!hwnd)
            return;
        if (!m_embedded) {
            m_pollTimer.setInterval(500);
            adoptWindow(qintptr(hwnd));
            return;
        }
        // A late-created main window (or a larger window replacing a splash)
        // must be adopted too, otherwise it stays as a separate top-level window.
        if (qintptr(hwnd) == m_childHwnd)
            return;
        RECT candidate{};
        RECT current{};
        if (!GetWindowRect(hwnd, &candidate) || !GetClientRect(reinterpret_cast<HWND>(m_childHwnd), &current))
            return;
        const qint64 candidateArea = qint64(candidate.right - candidate.left) * (candidate.bottom - candidate.top);
        const qint64 currentArea = qint64(current.right - current.left) * (current.bottom - current.top);
        if (candidateArea > currentArea * 12 / 10) {
            const qintptr oldChild = m_childHwnd;
            adoptWindow(qintptr(hwnd));
            if (oldChild && oldChild != m_childHwnd)
                ShowWindow(reinterpret_cast<HWND>(oldChild), SW_HIDE);
        }
#endif
    });

    // Some tools re-apply their own size right after being shown; keep forcing
    // our geometry for a short settle period so the embedded window matches the
    // area reserved by QML.
    m_settleTimer.setInterval(120);
    m_settleTimer.setSingleShot(false);
    connect(&m_settleTimer, &QTimer::timeout, this, [this]() {
        applyGeometry();
        measureChild();
    });

    m_geometryTimer.setInterval(30);
    m_geometryTimer.setSingleShot(true);
    connect(&m_geometryTimer, &QTimer::timeout, this, [this]() { applyGeometry(); });

    m_extraTimer.setInterval(400);
    m_extraTimer.setSingleShot(false);
    connect(&m_extraTimer, &QTimer::timeout, this, [this]() { adoptExtraWindows(); });
}

EmbeddedApp::~EmbeddedApp()
{
    close();
}

void EmbeddedApp::setMainWindow(QWindow* window)
{
    m_window = window;
    if (!m_window)
        return;
    connect(m_window, &QWindow::xChanged, this, [this]() { applyGeometry(); });
    connect(m_window, &QWindow::yChanged, this, [this]() { applyGeometry(); });
    connect(m_window, &QWindow::widthChanged, this, [this]() { applyGeometry(); });
    connect(m_window, &QWindow::heightChanged, this, [this]() { applyGeometry(); });
}

bool EmbeddedApp::start(const QString& exePath, const QStringList& arguments, const QString& workingDir,
                        int x, int y, int w, int h)
{
    close();
    m_error.clear();
    m_title.clear();
    m_childWidth = 0;
    m_childHeight = 0;
    m_nativeSizeValid = false;
    m_areaX = x;
    m_areaY = y;
    m_areaW = w;
    m_areaH = h;

    if (!QFileInfo::exists(exePath)) {
        setError(QStringLiteral("找不到工具：") + exePath);
        return false;
    }

    qint64 pid = 0;
#ifdef Q_OS_WIN
    // Vendor tools are usually single-instance. A stale instance would either
    // forward our launch to it (leaving a separate window) or refuse to embed,
    // so close the old one and start a fresh, hidden process that we can adopt.
    const qint64 existingPid = findProcessByExe(exePath);
    if (existingPid > 0) {
        const HANDLE stale = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(existingPid));
        if (stale) {
            TerminateProcess(stale, 0);
            WaitForSingleObject(stale, 1500);
            CloseHandle(stale);
        }
        for (int i = 0; i < 30; ++i) {
            if (findProcessByExe(exePath) == 0)
                break;
            QThread::msleep(100);
        }
        QThread::msleep(200);
    }
    const LaunchResult launched = launchHidden(exePath, arguments, workingDir);
    if (launched.ok)
        pid = launched.pid;
#endif
    if (pid == 0) {
        qint64 fallbackPid = 0;
        if (!QProcess::startDetached(exePath, arguments, workingDir, &fallbackPid) || fallbackPid == 0) {
            setError(QStringLiteral("无法启动工具：") + exePath);
            return false;
        }
        pid = fallbackPid;
    }
    m_pid = pid;
    m_pollTimer.start();
    emit stateChanged();
    return true;
}

qintptr EmbeddedApp::ourHwnd() const
{
#ifdef Q_OS_WIN
    if (m_window)
        return qintptr(m_window->winId());
#endif
    return 0;
}

void EmbeddedApp::adoptWindow(qintptr handle)
{
#ifdef Q_OS_WIN
    HWND child = reinterpret_cast<HWND>(handle);
    HWND parent = reinterpret_cast<HWND>(ourHwnd());
    if (!child || !parent)
        return;

    wchar_t titleBuf[512] = {0};
    GetWindowTextW(child, titleBuf, 511);
    m_title = QString::fromWCharArray(titleBuf);

    // Remember the vendor window's own size before we resize it. Feeding the
    // resized client size back into QML made fixed-layout tools (SP Flash)
    // grow a large white canvas around their UI.
    RECT initialRect{};
    if (GetWindowRect(child, &initialRect)) {
        const qreal dpr = m_window ? m_window->devicePixelRatio() : 1.0;
        m_childWidth = qMax(360, int((initialRect.right - initialRect.left) / dpr));
        m_childHeight = qMax(240, int((initialRect.bottom - initialRect.top) / dpr));
        m_nativeSizeValid = true;
    }

    LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU
               | WS_OVERLAPPED);
    style |= WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN;
    SetWindowLongPtrW(child, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtrW(child, GWL_EXSTYLE);
    exStyle &= ~(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME);
    SetWindowLongPtrW(child, GWL_EXSTYLE, exStyle);

    if (IsIconic(child))
        ShowWindow(child, SW_RESTORE);
    ShowWindow(child, SW_HIDE);
    SetParent(child, parent);
    m_childHwnd = handle;
    m_embedded = true;
    m_embeddedHidden = false;

    applyGeometry();
    ShowWindow(child, SW_SHOW);
    m_settleTimer.start();
    // Dialogs / secondary windows of the tool (e.g. SP Flash Tool's startup
    // warning) are separate top-level windows: pull them into the embedded
    // frame as well so nothing appears outside the main window.
    m_extraTimer.start();
    QTimer::singleShot(3000, this, [this]() {
        m_settleTimer.stop();
        adoptExtraWindows();
        measureChild();
    });
#endif
    emit stateChanged();
}

void EmbeddedApp::adoptExtraWindows()
{
#ifdef Q_OS_WIN
    if (!m_embedded || !m_window)
        return;
    const HWND parent = reinterpret_cast<HWND>(ourHwnd());
    const HWND mainWindow = reinterpret_cast<HWND>(m_childHwnd);
    if (!parent || !mainWindow)
        return;

    struct Ctx {
        DWORD pid;
        HWND parent;
        HWND main;
        QList<qintptr>* list;
        int areaX;
        int areaY;
        int areaW;
        int areaH;
    } ctx{DWORD(m_pid), parent, mainWindow, &m_extraWindows, m_areaX, m_areaY, m_areaW, m_areaH};

    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (!isProcessInTree(pid, c->pid) || hwnd == c->main)
                return TRUE;
            if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
                return TRUE;
            if (GetParent(hwnd) == c->parent)
                return TRUE;
            if (!IsWindowVisible(hwnd))
                return TRUE;
            RECT rect{};
            if (!GetWindowRect(hwnd, &rect))
                return TRUE;
            const int w = rect.right - rect.left;
            const int h = rect.bottom - rect.top;
            if (w < 80 || h < 40)
                return TRUE;

            // Only pull real dialogs into the frame. Small ownerless helper
            // overlays (splash/scroll/IME windows) cause black rectangles at
            // the window edges when reparented, so hide them instead.
            wchar_t classBuf[128] = {0};
            GetClassNameW(hwnd, classBuf, 127);
            wchar_t titleBuf[256] = {0};
            GetWindowTextW(hwnd, titleBuf, 255);
            const bool isDialog = QString::fromWCharArray(classBuf).compare(QStringLiteral("#32770"), Qt::CaseInsensitive) == 0
                                  || GetWindow(hwnd, GW_OWNER) != nullptr;
            const bool isMainLike = titleBuf[0] != 0 && w >= 300 && h >= 200;
            if (!isDialog && !isMainLike) {
                ShowWindow(hwnd, SW_HIDE);
                return TRUE;
            }

            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU
                       | WS_OVERLAPPED);
            style |= WS_CHILD | WS_VISIBLE;
            SetWindowLongPtrW(hwnd, GWL_STYLE, style);
            LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            exStyle &= ~(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
            SetParent(hwnd, c->parent);

            const qreal dpr = 1.0;
            const int areaX = int(c->areaX * dpr);
            const int areaY = int(c->areaY * dpr);
            const int areaW = int(c->areaW * dpr);
            const int areaH = int(c->areaH * dpr);
            const int x = areaX + qMax(0, (areaW - w) / 2);
            const int y = areaY + qMax(0, (areaH - h) / 2);
            SetWindowPos(hwnd, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
            c->list->append(qintptr(hwnd));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));

    // Anything still visible outside our frame would look like a "new window".
    // Hide leftovers from the same process tree after the real dialogs were adopted.
    struct HideCtx {
        DWORD pid;
        HWND parent;
        HWND main;
        QList<qintptr>* adopted;
    } hideCtx{DWORD(m_pid), parent, mainWindow, &m_extraWindows};
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* hc = reinterpret_cast<HideCtx*>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (!isProcessInTree(pid, hc->pid) || hwnd == hc->main || hwnd == hc->parent)
                return TRUE;
            if (!IsWindowVisible(hwnd))
                return TRUE;
            if (hc->adopted->contains(qintptr(hwnd)))
                return TRUE;
            if (GetParent(hwnd) == hc->parent)
                return TRUE;
            ShowWindow(hwnd, SW_HIDE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&hideCtx));
#endif
}

void EmbeddedApp::updateDimOverlay()
{
#ifdef Q_OS_WIN
    if (!m_embedded || !m_childHwnd || !m_window) {
        if (m_dimHwnd) {
            DestroyWindow(reinterpret_cast<HWND>(m_dimHwnd));
            m_dimHwnd = 0;
        }
        return;
    }
    if (!m_dimHwnd) {
        m_dimHwnd = qintptr(CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"STATIC", nullptr, WS_POPUP | SS_BLACKRECT, 0, 0, 1, 1,
            reinterpret_cast<HWND>(ourHwnd()), nullptr, nullptr, nullptr));
        if (!m_dimHwnd)
            return;
        SetLayeredWindowAttributes(reinterpret_cast<HWND>(m_dimHwnd), 0, 70, LWA_ALPHA);
    }
    POINT origin{int(m_areaX), int(m_areaY)};
    ClientToScreen(reinterpret_cast<HWND>(ourHwnd()), &origin);
    SetWindowPos(reinterpret_cast<HWND>(m_dimHwnd), HWND_TOP,
                 origin.x, origin.y, int(m_areaW), int(m_areaH),
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
#endif
}

void EmbeddedApp::applyGeometry()
{
#ifdef Q_OS_WIN
    if (!m_embedded || !m_childHwnd || !m_window)
        return;
    const qreal dpr = m_window->devicePixelRatio();
    const int x = int(m_areaX * dpr);
    const int y = int(m_areaY * dpr);
    const int w = int(m_areaW * dpr);
    const int h = int(m_areaH * dpr);
    if (w <= 0 || h <= 0)
        return;
    SetWindowPos(reinterpret_cast<HWND>(m_childHwnd), HWND_TOP, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    updateDimOverlay();
#endif
}

void EmbeddedApp::measureChild()
{
#ifdef Q_OS_WIN
    if (!m_embedded || !m_childHwnd || m_nativeSizeValid)
        return;
    RECT rect{};
    if (!GetClientRect(reinterpret_cast<HWND>(m_childHwnd), &rect))
        return;
    const qreal dpr = m_window ? m_window->devicePixelRatio() : 1.0;
    const int width = int(qRound(double(rect.right - rect.left) / dpr));
    const int height = int(qRound(double(rect.bottom - rect.top) / dpr));
    if (width == m_childWidth && height == m_childHeight)
        return;
    m_childWidth = width;
    m_childHeight = height;
    emit childSizeChanged();
#endif
}

void EmbeddedApp::updateArea(int x, int y, int w, int h)
{
    m_areaX = x;
    m_areaY = y;
    m_areaW = w;
    m_areaH = h;
    m_geometryTimer.start();
}

void EmbeddedApp::detachToDesktop()
{
#ifdef Q_OS_WIN
    HWND child = reinterpret_cast<HWND>(m_childHwnd);
    if (!child)
        return;
    m_settleTimer.stop();
    SetParent(child, nullptr);
    LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
    style &= ~WS_CHILD;
    style |= WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    SetWindowLongPtrW(child, GWL_STYLE, style);
    SetWindowPos(child, nullptr, 80, 80, 1200, 800, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
#endif
    m_embedded = false;
    emit stateChanged();
}

void EmbeddedApp::hideEmbedded(bool hidden)
{
#ifdef Q_OS_WIN
    HWND child = reinterpret_cast<HWND>(m_childHwnd);
    if (!child)
        return;
    m_embeddedHidden = hidden;
    ShowWindow(child, hidden ? SW_HIDE : SW_SHOW);
#endif
}

void EmbeddedApp::close()
{
#ifdef Q_OS_WIN
    HWND child = reinterpret_cast<HWND>(m_childHwnd);
    if (child)
        PostMessageW(child, WM_CLOSE, 0, 0);
    if (m_pid) {
        const HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(m_pid));
        if (process) {
            const DWORD wait = WaitForSingleObject(process, 250);
            if (wait != WAIT_OBJECT_0)
                TerminateProcess(process, 0);
            CloseHandle(process);
        }
    }
#endif
    m_settleTimer.stop();
    m_pollTimer.stop();
    m_extraTimer.stop();
    if (m_dimHwnd) {
        DestroyWindow(reinterpret_cast<HWND>(m_dimHwnd));
        m_dimHwnd = 0;
    }
    if (m_foreignProcess) {
        if (child) {
            SetParent(child, nullptr);
            LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
            style &= ~WS_CHILD;
            style |= WS_OVERLAPPEDWINDOW | WS_VISIBLE;
            SetWindowLongPtrW(child, GWL_STYLE, style);
            SetWindowPos(child, nullptr, 80, 80, 1024, 700, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        m_extraWindows.clear();
        m_childHwnd = 0;
        m_embedded = false;
        m_pid = 0;
        m_foreignProcess = false;
        m_childWidth = 0;
        m_childHeight = 0;
        m_nativeSizeValid = false;
        emit stateChanged();
        emit childSizeChanged();
        return;
    }
    for (const qintptr handle : m_extraWindows) {
#ifdef Q_OS_WIN
        HWND child = reinterpret_cast<HWND>(handle);
        if (child) {
            SetParent(child, nullptr);
            LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
            style &= ~WS_CHILD;
            style |= WS_OVERLAPPEDWINDOW;
            SetWindowLongPtrW(child, GWL_STYLE, style);
            PostMessageW(child, WM_CLOSE, 0, 0);
        }
#endif
    }
    m_extraWindows.clear();
    m_childHwnd = 0;
    m_embedded = false;
    m_pid = 0;
    m_title.clear();
    m_childWidth = 0;
    m_childHeight = 0;
    m_nativeSizeValid = false;
    emit stateChanged();
    emit childSizeChanged();
}

void EmbeddedApp::terminate()
{
#ifdef Q_OS_WIN
    if (m_pid) {
        const HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(m_pid));
        if (process) {
            TerminateProcess(process, 0);
            CloseHandle(process);
        }
    }
#endif
    m_childHwnd = 0;
    m_embedded = false;
    m_pid = 0;
    emit stateChanged();
}

QString EmbeddedApp::nativeWindowId() const
{
    return QString::number(m_childHwnd);
}

void EmbeddedApp::setError(const QString& error)
{
    m_error = error;
    emit stateChanged();
}

} // namespace gf
