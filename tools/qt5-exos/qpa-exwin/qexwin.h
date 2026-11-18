/* =============================================================================
 * tools/qt5-exos/qpa-exwin/qexwin.h - the "exwin" platform plugin of Qt 5
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-or-later
 *
 * Qt's windows as windows of ExWin (drivers/wserver/win_proto.h):
 *   - every QWindow shown is a window of the server, a shared memory zone of
 *     32-bit pixels; the backing store is a QImage ON that zone, and a flush
 *     is WIN_MSG_AGGIORNA of the region painted;
 *   - the server talks through the IPC mailbox, which poll() takes as
 *     FD_IPC: a QSocketNotifier on it wakes Qt's own event loop, no thread;
 *   - keys come as presses and releases (WIN_MSG_TASTI), the pointer's
 *     movement over the window too (WIN_MSG_PASSAGGIO);
 *   - menus, tooltips and combo lists are WIN_ST_COMPARSA windows at the
 *     place Qt asks; a click elsewhere closes them (WIN_EV_CHIUDI).
 * ============================================================================= */
#ifndef QEXWIN_H
#define QEXWIN_H

#include <qpa/qplatformintegration.h>
#include <qpa/qplatformscreen.h>
#include <qpa/qplatformwindow.h>
#include <qpa/qplatformbackingstore.h>
#include <QtGui/QImage>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QScopedPointer>

QT_BEGIN_NAMESPACE

class QSocketNotifier;
class QExwinWindow;

class QExwinScreen : public QPlatformScreen
{
public:
    QExwinScreen();
    QRect geometry() const override { return m_geometria; }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }
    QSizeF physicalSize() const override;
    QString name() const override { return QStringLiteral("exwin"); }

    QRect m_geometria;
};

class QExwinIntegration : public QObject, public QPlatformIntegration
{
    Q_OBJECT
public:
    QExwinIntegration();
    ~QExwinIntegration();

    unsigned int server() const { return m_server; }
    static QExwinIntegration *qui();

    void initialize() override;
    bool hasCapability(QPlatformIntegration::Capability cap) const override;
    QPlatformWindow *createPlatformWindow(QWindow *window) const override;
    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override;
    QAbstractEventDispatcher *createEventDispatcher() const override;
    QPlatformFontDatabase *fontDatabase() const override;
    QStringList themeNames() const override;

    /* the windows of the server, by id: who an event is for */
    void registra(unsigned int id, QExwinWindow *w) { m_finestre.insert(id, w); }
    void dimentica(unsigned int id) { m_finestre.remove(id); }

public Q_SLOTS:
    void posta();       /* every message of the server waiting, handed on */

private:
    void messaggio(unsigned int tipo, const unsigned char *dati, unsigned int len);

    unsigned int m_server;
    QExwinScreen *m_schermo;
    QSocketNotifier *m_notifica;
    mutable QPlatformFontDatabase *m_caratteri;
    QHash<unsigned int, QExwinWindow *> m_finestre;
};

class QExwinWindow : public QPlatformWindow
{
public:
    explicit QExwinWindow(QWindow *window);
    ~QExwinWindow();

    void setVisible(bool visible) override;
    void setGeometry(const QRect &rect) override;
    void setWindowTitle(const QString &title) override;
    void raise() override;
    void requestActivateWindow() override;
    void setWindowState(Qt::WindowStates state) override;
    WId winId() const override { return WId(m_id); }

    /* the server's zone: the backing store paints here */
    uchar *zona() const { return m_zona; }
    int passo() const { return int(m_passo); }
    QSize misuraZona() const { return QSize(int(m_zw), int(m_zh)); }
    void aggiorna(const QRect &r);

    /* from QExwinIntegration::messaggio */
    void misurata(const void *creata);
    void evento(const void *evento);
    void posizione(int x, int y);

private:
    void crea();
    void distruggi();
    bool attacca(const void *creata);
    bool comparsa() const;
    QPoint dentro(const QRect &g) const;  /* where it goes, title bar on the screen */

    unsigned int m_id;
    unsigned int m_giro;
    uchar *m_zona;
    unsigned int m_zw, m_zh, m_passo;
    Qt::MouseButtons m_bottoni;
    QPoint m_ultimo;            /* where the pointer was, inside the window */
};

class QExwinBackingStore : public QPlatformBackingStore
{
public:
    explicit QExwinBackingStore(QWindow *window);

    QPaintDevice *paintDevice() override;
    void flush(QWindow *window, const QRegion &region, const QPoint &offset) override;
    void resize(const QSize &size, const QRegion &staticContents) override;
    void beginPaint(const QRegion &region) override;

private:
    void rifai();

    QImage m_immagine;          /* on the zone when it fits, else its own */
    QSize m_misura;
};

QT_END_NAMESPACE

#endif
