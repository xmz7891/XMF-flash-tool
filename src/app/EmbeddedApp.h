#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QList>
#include <QTimer>

class QWindow;

namespace gf {

/// Runs an external vendor GUI tool and reparents its top-level window into the
/// main XMF window, so "official tools" feel built-in instead of opening a
/// second window. The tool is started hidden and only shown after it has been
/// adopted, avoiding a flash of an external window.
class EmbeddedApp : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool embedded READ embedded NOTIFY stateChanged)
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(int childWidth READ childWidth NOTIFY childSizeChanged)
    Q_PROPERTY(int childHeight READ childHeight NOTIFY childSizeChanged)
    Q_PROPERTY(bool childSizeValid READ childSizeValid NOTIFY childSizeChanged)

public:
    explicit EmbeddedApp(QObject* parent = nullptr);
    ~EmbeddedApp() override;

    bool running() const { return m_pid != 0; }
    bool embedded() const { return m_embedded; }
    QString title() const { return m_title; }
    QString error() const { return m_error; }
    int childWidth() const { return m_childWidth; }
    int childHeight() const { return m_childHeight; }
    bool childSizeValid() const { return m_childWidth > 0 && m_childHeight > 0; }

    void setMainWindow(QWindow* window);

    Q_INVOKABLE bool start(const QString& exePath, const QStringList& arguments,
                           const QString& workingDir, int x, int y, int w, int h);
    Q_INVOKABLE void updateArea(int x, int y, int w, int h);
    Q_INVOKABLE void detachToDesktop();
    Q_INVOKABLE void close();
    Q_INVOKABLE void terminate();
    Q_INVOKABLE void hideEmbedded(bool hidden);
    Q_INVOKABLE QString nativeWindowId() const;

signals:
    void stateChanged();
    void childSizeChanged();

private:
    void adoptWindow(qintptr handle);
    void adoptExtraWindows();
    void applyGeometry();
    void measureChild();
    void updateDimOverlay();
    void setError(const QString& error);
    qintptr ourHwnd() const;

    QPointer<QWindow> m_window;
    qint64 m_pid = 0;
    qintptr m_childHwnd = 0;
    qintptr m_dimHwnd = 0;
    bool m_embedded = false;
    bool m_embeddedHidden = false;
    bool m_nativeSizeValid = false;
    bool m_foreignProcess = false;
    QString m_title;
    QString m_error;
    int m_childWidth = 0;
    int m_childHeight = 0;
    QTimer m_pollTimer;
    QTimer m_settleTimer;
    QTimer m_geometryTimer;
    QTimer m_extraTimer;
    QList<qintptr> m_extraWindows;
    int m_areaX = 0;
    int m_areaY = 0;
    int m_areaW = 0;
    int m_areaH = 0;
};

} // namespace gf
