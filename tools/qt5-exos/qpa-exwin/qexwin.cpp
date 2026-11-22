/* =============================================================================
 * tools/qt5-exos/qpa-exwin/qexwin.cpp - the "exwin" platform plugin of Qt 5
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-or-later
 *
 * See qexwin.h. The protocol is drivers/wserver/win_proto.h, the same one
 * VLC's video output (tools/vlc-exos/src/vout_exos.c) and Exilla's widget
 * (tools/exilla/patch/widget_exos_*) speak.
 * ============================================================================= */
#include "qexwin.h"

#include <QtCore/QSocketNotifier>
#include <QtCore/QTimer>
#include <QtGui/QPainter>
#include <QtGui/QWindow>
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/private/qwindow_p.h>
#include <qpa/qwindowsysteminterface.h>
#include <qpa/qplatformfontdatabase.h>
#include <qpa/qplatformtheme.h>
#include <QtEventDispatcherSupport/private/qgenericunixeventdispatcher_p.h>
#if QT_CONFIG(freetype)
#include <QtFontDatabaseSupport/private/qfreetypefontdatabase_p.h>
#endif

#include <stdio.h>
#include <string.h>
#include <libc.h>
#include <exos/win_proto.h>
#include <exos/kbd_proto.h>

QT_BEGIN_NAMESPACE

/* how long the server may take to answer WIN_MSG_CREA */
static const unsigned int ATTESA_MS = 15000;

static QExwinIntegration *g_integrazione = nullptr;

/* QT_EXWIN_DEBUG=1: every message of the server on stderr, and what it was
 * handed to (the way tools/qt5-exos/prova.sh found out what a click did) */
static bool g_racconta = qEnvironmentVariableIntValue("QT_EXWIN_DEBUG") != 0;

/* every message to the server: with QT_EXWIN_DEBUG, the ones refused or slow
 * (a full mailbox makes ipc_send wait, then give up with -EBUSY) */
static int manda(unsigned int dest, unsigned int tipo, const void *dati, unsigned int n)
{
    if (!g_racconta)
        return ipc_send(dest, tipo, dati, n);
    const unsigned int t0 = uptime_ms();
    const int r = ipc_send(dest, tipo, dati, n);
    const unsigned int dt = uptime_ms() - t0;
    if (r < 0 || dt > 100)
        fprintf(stderr, "exwin: ipc_send 0x%x -> %d in %u ms\n", tipo, r, dt);
    return r;
}

/* the mouse buttons of the server (MOUSE_BTN_* of the mouse service) */
static Qt::MouseButtons bottoni_qt(unsigned int b)
{
    Qt::MouseButtons q = Qt::NoButton;
    if (b & 1u) q |= Qt::LeftButton;
    if (b & 2u) q |= Qt::RightButton;
    if (b & 4u) q |= Qt::MiddleButton;
    return q;
}

static Qt::KeyboardModifiers modificatori_qt(unsigned int m)
{
    Qt::KeyboardModifiers q = Qt::NoModifier;
    if (m & KBD_MOD_SHIFT) q |= Qt::ShiftModifier;
    if (m & KBD_MOD_CTRL)  q |= Qt::ControlModifier;
    if (m & KBD_MOD_ALT)   q |= Qt::AltModifier;
    if (m & KBD_MOD_SUPER) q |= Qt::MetaModifier;
    return q;
}

/* ---------------------------------------------------------------------------
 * The screen
 * ------------------------------------------------------------------------- */
QExwinScreen::QExwinScreen()
{
    VideoInfo v;

    if (video_info(&v) != 0 || v.larghezza == 0 || v.altezza == 0) {
        v.larghezza = 1024;
        v.altezza = 768;
    }
    m_geometria = QRect(0, 0, int(v.larghezza), int(v.altezza));
}

/* 96 dpi: the server does not know the monitor, and Qt computes the logical
 * dpi from this */
QSizeF QExwinScreen::physicalSize() const
{
    return QSizeF(m_geometria.width() * 25.4 / 96.0, m_geometria.height() * 25.4 / 96.0);
}

/* ---------------------------------------------------------------------------
 * The integration
 * ------------------------------------------------------------------------- */
QExwinIntegration::QExwinIntegration()
    : m_server(0), m_schermo(nullptr), m_notifica(nullptr), m_caratteri(nullptr)
{
    char nome[32];
    int pid;

    win_nome_servizio(nome, sizeof(nome));
    pid = ipc_lookup(nome);
    if (pid <= 0) {
        qWarning("exwin: no window server (%s): ExWin is not running", nome);
        return;
    }
    m_server = unsigned(pid);
    g_integrazione = this;
    m_schermo = new QExwinScreen;
    QWindowSystemInterface::handleScreenAdded(m_schermo);
}

QExwinIntegration::~QExwinIntegration()
{
    if (m_schermo)
        QWindowSystemInterface::handleScreenRemoved(m_schermo);
    delete m_caratteri;
    delete m_nativa;
    if (g_integrazione == this)
        g_integrazione = nullptr;
}

QExwinIntegration *QExwinIntegration::qui()
{
    return g_integrazione;
}

/* ! FD_IPC IS NOT A REAL DESCRIPTOR (lib/include/libc.h, poll): only poll and
 * select know it, which is all QSocketNotifier needs. */
void QExwinIntegration::initialize()
{
    m_notifica = new QSocketNotifier(FD_IPC, QSocketNotifier::Read, this);
    connect(m_notifica, &QSocketNotifier::activated, this, &QExwinIntegration::posta);
}

bool QExwinIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
    case ThreadedPixmaps: return true;
    case MultipleWindows: return true;
    case NonFullScreenWindows: return true;
    case WindowManagement: return true;
    default: return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformWindow *QExwinIntegration::createPlatformWindow(QWindow *window) const
{
    if (g_racconta)
        fprintf(stderr, "exwin: finestra %p padre %p %s %dx%d\n", (void *)window, (void *)window->parent(),
                window->objectName().toLocal8Bit().constData(), window->width(), window->height());
    return new QExwinWindow(window);
}

QPlatformNativeInterface *QExwinIntegration::nativeInterface() const
{
    if (!m_nativa)
        m_nativa = new QExwinNative;
    return m_nativa;
}

void *QExwinNative::nativeResourceForWindow(const QByteArray &resource, QWindow *window)
{
    if (!window || resource != "exwin")
        return nullptr;
    if (!window->handle())
        window->create();
    QExwinWindow *w = static_cast<QExwinWindow *>(window->handle());
    return w ? reinterpret_cast<void *>(uintptr_t(w->serverIdOra())) : nullptr;
}

QPlatformBackingStore *QExwinIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new QExwinBackingStore(window);
}

QAbstractEventDispatcher *QExwinIntegration::createEventDispatcher() const
{
    return createUnixEventDispatcher();
}

/* the fonts in /exos/qt5/lib/fonts (DejaVu), or QT_QPA_FONTDIR */
QPlatformFontDatabase *QExwinIntegration::fontDatabase() const
{
    if (!m_caratteri) {
#if QT_CONFIG(freetype)
        m_caratteri = new QFreeTypeFontDatabase;
#else
        m_caratteri = QPlatformIntegration::fontDatabase();
#endif
    }
    return m_caratteri;
}

QStringList QExwinIntegration::themeNames() const
{
    return QStringList();
}

static int dal_server(const IpcMessage *m, void *dato)
{
    return m->sender_pid == *static_cast<unsigned int *>(dato) ? IPC_MIO : IPC_ALTRUI;
}

/* ! THE SHELF TOO, NOT ONLY THE KERNEL'S QUEUE: poll looks at the kernel, and
 * a message put aside while waiting for WIN_MSG_CREATA would stay there until
 * the next one arrives (libc.h, ipc_pronto). */
void QExwinIntegration::posta()
{
    IpcMessage meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int s = m_server;

    if (g_racconta)
        fprintf(stderr, "exwin: posta\n");
    for (int giri = 0; giri < 256; giri++) {
        if (ipc_scegli(dal_server, &s, &meta, buf, sizeof(buf), IPC_SUBITO) < 0)
            break;
        messaggio(meta.tipo, buf, meta.len);
    }
    QWindowSystemInterface::flushWindowSystemEvents();
}

void QExwinIntegration::messaggio(unsigned int tipo, const unsigned char *dati, unsigned int len)
{
    if (g_racconta) {
        const unsigned int *u = reinterpret_cast<const unsigned int *>(dati);
        fprintf(stderr, "exwin: messaggio 0x%x id %u (%u %u %u %u)%s\n", tipo, len >= 4 ? u[0] : 0,
                len >= 8 ? u[1] : 0, len >= 12 ? u[2] : 0, len >= 16 ? u[3] : 0, len >= 20 ? u[4] : 0,
                len >= 4 && !m_finestre.contains(u[0]) ? " - di nessuna finestra" : "");
    }
    switch (tipo) {
    case WIN_MSG_EVENTO:
        if (len >= sizeof(WinEvento)) {
            const WinEvento *e = reinterpret_cast<const WinEvento *>(dati);
            if (QExwinWindow *w = m_finestre.value(e->id))
                w->evento(e);
        }
        break;
    case WIN_MSG_MISURATA:
        if (len >= sizeof(WinCreata)) {
            const WinCreata *c = reinterpret_cast<const WinCreata *>(dati);
            if (QExwinWindow *w = m_finestre.value(c->id))
                w->misurata(c);
        }
        break;
    case WIN_MSG_POSTA:
        if (len >= sizeof(WinRegione)) {
            const WinRegione *r = reinterpret_cast<const WinRegione *>(dati);
            if (QExwinWindow *w = m_finestre.value(r->id))
                w->posizione(int(r->x), int(r->y));
        }
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * The window
 * ------------------------------------------------------------------------- */
QExwinWindow::QExwinWindow(QWindow *window)
    : QPlatformWindow(window), m_id(0), m_giro(0), m_zona(nullptr),
      m_zw(0), m_zh(0), m_passo(0), m_bottoni(Qt::NoButton)
{
}

QExwinWindow::~QExwinWindow()
{
    distruggi();
}

/* The frame the server draws round a window with a title: the title bar is
 * BARRA_H + BORDO above the client area, BORDO to the left (both are in
 * drivers/wserver/wserver.c, not in the protocol). */
static const int CORNICE_SU = 20 + 2, CORNICE_LATO = 2;

/* ! WHERE QT ASKS, BUT WITH THE TITLE BAR ON THE SCREEN: Qt centres a dialog
 * on its parent, and VLC's first one came out at a negative y; clamped to 0
 * its client area started at the top and the title bar, the only handle to
 * move it, was off the screen. */
QPoint QExwinWindow::dentro(const QRect &g) const
{
    const QRect s = screen() ? screen()->geometry() : QRect(0, 0, 1024, 768);
    const int sx = comparsa() ? 0 : CORNICE_LATO, sy = comparsa() ? 0 : CORNICE_SU;
    int x = g.x(), y = g.y();

    if (x + g.width() > s.width() - sx) x = s.width() - sx - g.width();
    if (y + g.height() > s.height() - sx) y = s.height() - sx - g.height();
    return QPoint(qMax(sx, x), qMax(sy, y));
}

bool QExwinWindow::comparsa() const
{
    const Qt::WindowType t = window()->type();
    return t == Qt::Popup || t == Qt::ToolTip || t == Qt::SplashScreen
        || (window()->flags() & Qt::FramelessWindowHint);
}

static int solo_creata(const IpcMessage *m, void *dato)
{
    if (m->sender_pid != *static_cast<unsigned int *>(dato))
        return IPC_ALTRUI;
    return m->tipo == WIN_MSG_CREATA ? IPC_MIO : IPC_ALTRUI;
}

bool QExwinWindow::attacca(const void *p)
{
    const WinCreata *r = static_cast<const WinCreata *>(p);
    ShmZona z;

    memset(&z, 0, sizeof(z));
    win_nome_zona(z.nome, r->id, r->giro);
    z.byte = r->byte;
    if (shm_apri(&z) != 0 || !z.virt)
        return false;
    if (m_zona)
        shm_chiudi(m_zona);
    m_zona = reinterpret_cast<uchar *>(uintptr_t(z.virt));
    m_zw = r->larghezza;
    m_zh = r->altezza;
    m_passo = r->passo ? r->passo : r->larghezza * 4;
    m_giro = r->giro;
    return true;
}

/* ! A QWINDOW WITH A PARENT IS NOT A WINDOW OF ITS OWN (wserver 0.016). Every
 * QWindow used to become a top-level window of the server: a native child
 * widget - VLC's video with WA_NativeWindow - came out as a second window
 * with a title bar of its own, somewhere on the screen. Now it is a child
 * window (WIN_MSG_CREA_FIGLIA), drawn inside the parent and moved with it. */
QExwinWindow *QExwinWindow::madre() const
{
    QWindow *p = window()->parent();
    return p ? static_cast<QExwinWindow *>(p->handle()) : nullptr;
}

bool QExwinWindow::creaFiglia()
{
    QExwinIntegration *i = QExwinIntegration::qui();
    QExwinWindow *m = madre();
    if (!m || !m->m_id)
        return false;           /* made when the parent is: see figlie() */

    const QRect g = geometry();
    WinCreaFiglia c;
    if (g_racconta)
        fprintf(stderr, "exwin: chiedo una figlia di %u per %p, %dx%d a %d,%d\n", m->m_id,
                (void *)window(), g.width(), g.height(), g.x(), g.y());
    c.padre = m->m_id;
    c.x = unsigned(qMax(0, g.x()));
    c.y = unsigned(qMax(0, g.y()));
    c.larghezza = unsigned(qMax(1, g.width()));
    c.altezza = unsigned(qMax(1, g.height()));
    unsigned int s = i->server();
    if (manda(s, WIN_MSG_CREA_FIGLIA, &c, sizeof(c)) < 0)
        return false;
    IpcMessage meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    if (ipc_scegli(solo_creata, &s, &meta, buf, sizeof(buf), ATTESA_MS) < 0
        || meta.len < sizeof(WinCreata)) {
        qWarning("exwin: the window server did not answer WIN_MSG_CREA_FIGLIA");
        return false;
    }
    WinCreata r;
    memcpy(&r, buf, sizeof(r));
    if (g_racconta)
        fprintf(stderr, "exwin: figlia %u di %u\n", r.id, m->m_id);
    if (r.id == 0)
        return false;           /* an old wserver, or not our parent */
    m_id = r.id;
    if (!attacca(&r)) {
        WinRegione d = { r.id, 0, 0, 0, 0 };
        manda(s, WIN_MSG_DISTRUGGI, &d, sizeof(d));
        m_id = 0;
        return false;
    }
    i->registra(m_id, this);
    const QRect nuova(g.topLeft(), QSize(int(r.larghezza), int(r.altezza)));
    QPlatformWindow::setGeometry(nuova);
    QWindowSystemInterface::handleGeometryChange(window(), nuova);
    QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), nuova.size()));
    QTimer::singleShot(0, i, &QExwinIntegration::posta);
    return true;
}

/* the native children follow the server window of their parent: made after
 * it, and forgotten with it (the server closes them together) */
void QExwinWindow::figlie(bool fai)
{
    const QObjectList &l = window()->children();
    for (QObject *o : l) {
        QWindow *f = qobject_cast<QWindow *>(o);
        QExwinWindow *pf = f ? static_cast<QExwinWindow *>(f->handle()) : nullptr;
        if (!pf)
            continue;
        if (fai) {
            if (f->isVisible())
                pf->crea();
        } else {
            pf->distruggi();
        }
    }
}

unsigned int QExwinWindow::serverIdOra()
{
    if (!m_id) {
        QExwinWindow *m = madre();
        if (m)
            m->serverIdOra();
        crea();
    }
    if (!m_id && g_racconta)
        fprintf(stderr, "exwin: nessun id per %p (padre %p)\n", (void *)window(), (void *)window()->parent());
    return m_id;
}

void QExwinWindow::crea()
{
    QExwinIntegration *i = QExwinIntegration::qui();
    if (m_id || !i)
        return;
    if (window()->parent()) {
        if (creaFiglia())
            figlie(true);
        return;
    }

    const QRect g = geometry();
    WinCrea c;
    memset(&c, 0, sizeof(c));
    /* a top-level window where the server likes, unless Qt was told where;
     * a menu or a tooltip exactly where Qt puts it */
    const bool posto = comparsa() || qt_window_private(window())->positionAutomatic == false;
    const QPoint qui = dentro(g);
    c.x = posto ? unsigned(qui.x()) : WIN_XY_AUTO;
    c.y = posto ? unsigned(qui.y()) : WIN_XY_AUTO;
    c.larghezza = unsigned(qMax(1, g.width()));
    c.altezza = unsigned(qMax(1, g.height()));
    c.stile = WIN_ST_VISIBILE;
    if (comparsa()) {
        c.stile |= WIN_ST_COMPARSA | WIN_ST_SOPRA;
    } else {
        c.stile |= WIN_ST_TITOLO | WIN_ST_BORDO | WIN_ST_CHIUDI;
        if (window()->minimumSize() != window()->maximumSize())
            c.stile |= WIN_ST_RIDIM;
        if (window()->modality() != Qt::NonModal)
            c.stile |= WIN_ST_MODALE;
        if (window()->flags() & Qt::WindowStaysOnTopHint)
            c.stile |= WIN_ST_SOPRA;
    }
    const QByteArray t = window()->title().toLocal8Bit();
    strncpy(c.titolo, t.isEmpty() ? "Qt" : t.constData(), WIN_TITOLO_LEN - 1);

    unsigned int s = i->server();
    if (manda(s, WIN_MSG_CREA, &c, sizeof(c)) < 0)
        return;
    IpcMessage meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    if (ipc_scegli(solo_creata, &s, &meta, buf, sizeof(buf), ATTESA_MS) < 0
        || meta.len < sizeof(WinCreata)) {
        qWarning("exwin: the window server did not answer WIN_MSG_CREA");
        return;
    }
    WinCreata r;
    memcpy(&r, buf, sizeof(r));
    if (r.id == 0)
        return;
    m_id = r.id;
    if (!attacca(&r)) {
        WinRegione d = { r.id, 0, 0, 0, 0 };
        manda(s, WIN_MSG_DISTRUGGI, &d, sizeof(d));
        m_id = 0;
        return;
    }
    i->registra(m_id, this);
    /* presses and releases, and the pointer moving over it */
    WinRegione on = { m_id, 1, 0, 0, 0 };
    manda(s, WIN_MSG_TASTI, &on, sizeof(on));
    manda(s, WIN_MSG_PASSAGGIO, &on, sizeof(on));

    const QRect nuova(int(r.x), int(r.y), int(r.larghezza), int(r.altezza));
    QPlatformWindow::setGeometry(nuova);
    QWindowSystemInterface::handleGeometryChange(window(), nuova);
    QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), nuova.size()));
    if (!comparsa())
        QWindowSystemInterface::handleWindowActivated(window());
    figlie(true);
    /* what was put on the shelf while waiting */
    QTimer::singleShot(0, i, &QExwinIntegration::posta);
}

void QExwinWindow::distruggi()
{
    QExwinIntegration *i = QExwinIntegration::qui();
    if (m_id)
        figlie(false);
    if (m_id && i) {
        WinRegione d = { m_id, 0, 0, 0, 0 };
        manda(i->server(), WIN_MSG_DISTRUGGI, &d, sizeof(d));
        i->dimentica(m_id);
    }
    m_id = 0;
    if (m_zona) {
        shm_chiudi(m_zona);
        m_zona = nullptr;
    }
}

void QExwinWindow::setVisible(bool visible)
{
    /* ! A CHILD IS HIDDEN, NOT CLOSED (WIN_MSG_MOSTRA): its id may be in
     * somebody else's hands - VLC's video output has the video widget's -
     * and Qt hides and shows widgets while it lays a window out */
    QExwinIntegration *i = QExwinIntegration::qui();
    if (window()->parent() && m_id && i) {
        WinRegione v = { m_id, visible ? 1u : 0u, 0, 0, 0 };
        manda(i->server(), WIN_MSG_MOSTRA, &v, sizeof(v));
        if (g_racconta)
            fprintf(stderr, "exwin: figlia %u %s\n", m_id, visible ? "mostrata" : "nascosta");
        if (!visible)
            QWindowSystemInterface::handleExposeEvent(window(), QRegion());
        else
            QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
        QPlatformWindow::setVisible(visible);
        return;
    }
    if (visible) {
        crea();
    } else {
        distruggi();
        QWindowSystemInterface::handleExposeEvent(window(), QRegion());
    }
    QPlatformWindow::setVisible(visible);
}

void QExwinWindow::setGeometry(const QRect &rect)
{
    const QRect prima = geometry();
    if (g_racconta)
        fprintf(stderr, "exwin: geometria %u %p %dx%d a %d,%d (era %dx%d a %d,%d)\n", m_id, (void *)window(),
                rect.width(), rect.height(), rect.x(), rect.y(), prima.width(), prima.height(), prima.x(), prima.y());
    QPlatformWindow::setGeometry(rect);
    QExwinIntegration *i = QExwinIntegration::qui();
    if (!m_id || !i) {
        QWindowSystemInterface::handleGeometryChange(window(), rect);
        return;
    }
    if (rect.topLeft() != prima.topLeft()) {
        /* a child: where Qt says inside the parent, no title bar to keep */
        const QPoint qui = window()->parent() ? QPoint(qMax(0, rect.x()), qMax(0, rect.y()))
                                              : dentro(rect);
        WinRegione p = { m_id, unsigned(qui.x()), unsigned(qui.y()), 0, 0 };
        manda(i->server(), WIN_MSG_SPOSTA, &p, sizeof(p));
    }
    if (rect.size() != prima.size()) {
        /* the zone comes back with WIN_MSG_MISURATA (misurata) */
        WinRegione m = { m_id, 0, 0, unsigned(qMax(1, rect.width())), unsigned(qMax(1, rect.height())) };
        manda(i->server(), WIN_MSG_MISURA, &m, sizeof(m));
    }
    QWindowSystemInterface::handleGeometryChange(window(), rect);
}

void QExwinWindow::setWindowTitle(const QString &title)
{
    QExwinIntegration *i = QExwinIntegration::qui();
    if (!m_id || !i || window()->parent())
        return;
    WinTitolo t;
    memset(&t, 0, sizeof(t));
    t.id = m_id;
    strncpy(t.titolo, title.toLocal8Bit().constData(), WIN_TITOLO_LEN - 1);
    manda(i->server(), WIN_MSG_TITOLO, &t, sizeof(t));
}

void QExwinWindow::raise()
{
    QExwinIntegration *i = QExwinIntegration::qui();
    if (m_id && i) {
        WinRegione r = { m_id, 0, 0, 0, 0 };
        manda(i->server(), WIN_MSG_PRIMO, &r, sizeof(r));
    }
}

void QExwinWindow::requestActivateWindow()
{
    QExwinIntegration *i = QExwinIntegration::qui();
    if (m_id && i) {
        WinRegione r = { m_id, 0, 0, 0, 0 };
        manda(i->server(), WIN_MSG_ATTIVA, &r, sizeof(r));
    }
    /* a child is never the active window: its top-level is */
    if (!window()->parent())
        QWindowSystemInterface::handleWindowActivated(window());
}

void QExwinWindow::setWindowState(Qt::WindowStates state)
{
    QExwinIntegration *i = QExwinIntegration::qui();
    QPlatformWindow::setWindowState(state);
    if (m_id && i && (state & Qt::WindowMinimized)) {
        WinRegione r = { m_id, 0, 0, 0, 0 };
        manda(i->server(), WIN_MSG_RIDUCI, &r, sizeof(r));
    } else if (state & (Qt::WindowMaximized | Qt::WindowFullScreen)) {
        setGeometry(screen()->availableGeometry());
    }
}

/* a new zone, asked by setGeometry or by the user dragging the corner */
void QExwinWindow::misurata(const void *p)
{
    const WinCreata *r = static_cast<const WinCreata *>(p);
    if (r->giro == m_giro || !attacca(r))
        return;
    QRect g = geometry();
    g.setSize(QSize(int(r->larghezza), int(r->altezza)));
    QPlatformWindow::setGeometry(g);
    QWindowSystemInterface::handleGeometryChange(window(), g);
    QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), g.size()));
}

void QExwinWindow::posizione(int x, int y)
{
    QRect g = geometry();
    g.moveTo(x, y);
    QPlatformWindow::setGeometry(g);
    QWindowSystemInterface::handleGeometryChange(window(), g);
}

/* the pixels are ready: the region, and with it the receipt of a new size */
void QExwinWindow::aggiorna(const QRect &r)
{
    QExwinIntegration *i = QExwinIntegration::qui();
    if (!m_id || !i)
        return;
    WinRegione u = { m_id, unsigned(r.x()), unsigned(r.y()), unsigned(r.width()), unsigned(r.height()) };
    manda(i->server(), WIN_MSG_AGGIORNA, &u, sizeof(u));
}

/* KBD_K_* and the control characters as Qt keys; 0 for a character */
static int tasto_speciale(unsigned int c)
{
    switch (c) {
    case KBD_K_UP:    return Qt::Key_Up;
    case KBD_K_DOWN:  return Qt::Key_Down;
    case KBD_K_LEFT:  return Qt::Key_Left;
    case KBD_K_RIGHT: return Qt::Key_Right;
    case KBD_K_HOME:  return Qt::Key_Home;
    case KBD_K_END:   return Qt::Key_End;
    case KBD_K_PGUP:  return Qt::Key_PageUp;
    case KBD_K_PGDN:  return Qt::Key_PageDown;
    case KBD_K_INS:   return Qt::Key_Insert;
    case KBD_K_DEL:   return Qt::Key_Delete;
    case KBD_K_WIN:   return Qt::Key_Meta;
    case KBD_K_STAMP: return Qt::Key_Print;
    case '\r': case '\n': return Qt::Key_Return;
    case '\t':        return Qt::Key_Tab;
    case 8:           return Qt::Key_Backspace;
    case 27:          return Qt::Key_Escape;
    default:
        if (c >= KBD_K_F1 && c <= KBD_K_F12)
            return Qt::Key_F1 + int(c - KBD_K_F1);
        return 0;
    }
}

/* a key that types nothing (a modifier alone), by its physical key */
static int tasto_fisico(unsigned int sc)
{
    switch (sc) {
    case 0x2A: case 0x36: return Qt::Key_Shift;
    case 0x1D: case 0x11D: return Qt::Key_Control;
    case 0x38: return Qt::Key_Alt;
    case 0x138: return Qt::Key_AltGr;
    case 0x3A: return Qt::Key_CapsLock;
    case 0x45: return Qt::Key_NumLock;
    case 0x15B: case 0x15C: return Qt::Key_Meta;
    case 0x15D: return Qt::Key_Menu;
    default: return 0;
    }
}

void QExwinWindow::evento(const void *p)
{
    const WinEvento *e = static_cast<const WinEvento *>(p);
    QWindow *w = window();
    const QPoint qui(int(e->x), int(e->y));
    const QPoint schermo = geometry().topLeft() + qui;
    const ulong t = e->tempo;

    if (g_racconta)
        fprintf(stderr, "exwin: evento %u a %u (%s) in %d,%d bottoni 0x%x tasto 0x%x\n", e->tipo, m_id,
                comparsa() ? "comparsa" : "finestra", qui.x(), qui.y(), e->bottoni, e->tasto);
    switch (e->tipo) {
    case WIN_EV_MOUSE_GIU:
        m_bottoni |= Qt::LeftButton;
        m_ultimo = qui;
        QWindowSystemInterface::handleMouseEvent(w, t, qui, schermo, m_bottoni, Qt::LeftButton,
                                                 QEvent::MouseButtonPress, modificatori_qt(e->tasto));
        break;
    case WIN_EV_MOUSE_SU:
        m_bottoni &= ~Qt::LeftButton;
        m_ultimo = qui;
        QWindowSystemInterface::handleMouseEvent(w, t, qui, schermo, m_bottoni, Qt::LeftButton,
                                                 QEvent::MouseButtonRelease, QGuiApplication::keyboardModifiers());
        /* dragged out and let go there: the leave the server will not say
         * (see WIN_EV_USCITO below) */
        if (!QRect(QPoint(), geometry().size()).contains(qui))
            QWindowSystemInterface::handleLeaveEvent(w);
        break;
    case WIN_EV_MOUSE_MOSSO:
        m_ultimo = qui;
        if (e->bottoni == 0 && m_bottoni)
            m_bottoni = Qt::NoButton;
        QWindowSystemInterface::handleMouseEvent(w, t, qui, schermo, m_bottoni, Qt::NoButton,
                                                 QEvent::MouseMove, QGuiApplication::keyboardModifiers());
        break;
    case WIN_EV_DESTRO:
        /* ! ONLY THE PRESS COMES (drivers/wserver/wserver.c): the release is
         * made here, at once, so that no button stays held in Qt */
        QWindowSystemInterface::handleMouseEvent(w, t, qui, schermo, m_bottoni | Qt::RightButton,
                                                 Qt::RightButton, QEvent::MouseButtonPress,
                                                 modificatori_qt(e->tasto));
        QWindowSystemInterface::handleMouseEvent(w, t, qui, schermo, m_bottoni, Qt::RightButton,
                                                 QEvent::MouseButtonRelease, modificatori_qt(e->tasto));
        break;
    case WIN_EV_ROTELLA: {
        /* steps in tasto, positive = "the page goes down": Qt's are negative */
        const int scatti = int(e->tasto);
        QWindowSystemInterface::handleWheelEvent(w, t, qui, schermo, QPoint(),
                                                 QPoint(0, -120 * scatti),
                                                 QGuiApplication::keyboardModifiers());
        break;
    }
    case WIN_EV_USCITO:
        /* ! A BUTTON PRESSED IS NOT A LEAVE: the server stops the passing
         * movement while a button is held (drivers/wserver/wserver.c,
         * passaggio) and says USCITO to the window, with the pointer still
         * on it. Qt's menu took it as a leave, dropped the item under the
         * pointer, and the release triggered nothing. A real leave is
         * somewhere off the client area. */
        if (!QRect(QPoint(), geometry().size()).contains(qui))
            QWindowSystemInterface::handleLeaveEvent(w);
        break;
    case WIN_EV_CHIUDI:
        QWindowSystemInterface::handleCloseEvent(w);
        break;
    case WIN_EV_DISEGNA:
        QWindowSystemInterface::handleExposeEvent(w, QRect(QPoint(), geometry().size()));
        break;
    case WIN_EV_PREMUTO:
    case WIN_EV_RILASCIATO:
    case WIN_EV_TASTO: {
        const bool giu = e->tipo != WIN_EV_RILASCIATO;
        const unsigned int c = e->tasto & KBD_KEY_MASK;
        const unsigned int mod = e->tipo == WIN_EV_TASTO ? e->tasto : e->bottoni;
        Qt::KeyboardModifiers m = modificatori_qt(mod);
        int k = tasto_speciale(c);
        QString testo;
        const uint uni = e->tipo == WIN_EV_TASTO ? (c >= 32 && c < 0x100 ? c : 0) : e->x;
        if (!k && uni >= 32 && uni != 127) {
            testo = QString(QChar(uni));
            k = QChar(uni).toUpper().unicode();
            if (k >= 'a' && k <= 'z') k -= 32;
        } else if (!k && c >= 1 && c <= 26 && (mod & KBD_MOD_CTRL)) {
            k = Qt::Key_A + int(c - 1);           /* Ctrl+letter */
        } else if (!k && c >= 'a' && c <= 'z') {
            k = Qt::Key_A + int(c - 'a');         /* Ctrl+a: 'a' | KBD_MOD_CTRL */
        }
        if (!k && e->tipo != WIN_EV_TASTO)
            k = tasto_fisico(e->y);
        if (!k)
            break;
        if (k == Qt::Key_Return || k == Qt::Key_Tab || k == Qt::Key_Backspace || k == Qt::Key_Escape) {
            static const char *testi[] = { "\r", "\t", "\b", "\x1b" };
            testo = QLatin1String(k == Qt::Key_Return ? testi[0] : k == Qt::Key_Tab ? testi[1]
                                  : k == Qt::Key_Backspace ? testi[2] : testi[3]);
        }
        if (m & Qt::ControlModifier)
            testo.clear();
        const bool ripetuto = e->tipo != WIN_EV_TASTO && (e->bottoni & KBD_EV_RIPETUTO);
        if (e->tipo == WIN_EV_TASTO) {
            /* an old kbd.drv: one event per key, press and release together */
            QWindowSystemInterface::handleKeyEvent(w, t, QEvent::KeyPress, k, m, testo);
            QWindowSystemInterface::handleKeyEvent(w, t, QEvent::KeyRelease, k, m, testo);
        } else {
            QWindowSystemInterface::handleKeyEvent(w, t, giu ? QEvent::KeyPress : QEvent::KeyRelease,
                                                   k, m, testo, ripetuto);
        }
        break;
    }
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * The backing store: Qt paints in its own image, a flush copies the region
 * into the server's zone and says so. The zone can change size under it
 * (WIN_MSG_MISURATA): the copy is clipped to the zone it has now.
 * ------------------------------------------------------------------------- */
QExwinBackingStore::QExwinBackingStore(QWindow *window)
    : QPlatformBackingStore(window)
{
}

QPaintDevice *QExwinBackingStore::paintDevice()
{
    return &m_immagine;
}

void QExwinBackingStore::beginPaint(const QRegion &region)
{
    if (g_racconta)
        fprintf(stderr, "exwin: dipinge %d,%d %dx%d\n", region.boundingRect().x(), region.boundingRect().y(),
                region.boundingRect().width(), region.boundingRect().height());
    if (m_immagine.hasAlphaChannel()) {
        QPainter p(&m_immagine);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        for (const QRect &r : region)
            p.fillRect(r, Qt::transparent);
    }
}

void QExwinBackingStore::resize(const QSize &size, const QRegion &staticContents)
{
    Q_UNUSED(staticContents);
    if (m_immagine.size() == size)
        return;
    m_misura = size;
    m_immagine = QImage(size, QImage::Format_RGB32);
}

void QExwinBackingStore::flush(QWindow *window, const QRegion &region, const QPoint &offset)
{
    QExwinWindow *w = static_cast<QExwinWindow *>(window->handle());
    if (!w || !w->zona()) {
        if (g_racconta)
            fprintf(stderr, "exwin: flush BUTTATO %p (padre %p) %d,%d %dx%d: senza zona\n", (void *)window,
                    (void *)window->parent(), region.boundingRect().x(), region.boundingRect().y(),
                    region.boundingRect().width(), region.boundingRect().height());
        return;
    }
    const QSize z = w->misuraZona();
    const QRect tutto(QPoint(), z);
    QRect fatto;
    /* ! THE REGION IS THE WINDOW'S, THE OFFSET IS WHERE THE WINDOW IS IN THE
     * IMAGE (as QXcbBackingStore::flush): the pixels are read at region +
     * offset, written at region. It was the other way round, and nobody saw
     * it while every window was a top-level (offset 0); a native child -
     * VLC's widgets with the video inside - had the top of the image in it,
     * the menu bar twice. */
    for (const QRect &r0 : region) {
        const QRect r = r0.intersected(tutto).intersected(
                            QRect(-offset, m_immagine.size()));
        if (r.isEmpty())
            continue;
        const int bpl = m_immagine.bytesPerLine();
        for (int y = r.top(); y <= r.bottom(); y++) {
            const uint *s = reinterpret_cast<const uint *>(m_immagine.constBits()
                            + (y + offset.y()) * bpl) + (r.left() + offset.x());
            uint *d = reinterpret_cast<uint *>(w->zona() + y * w->passo()) + r.left();
            for (int x = 0; x < r.width(); x++)
                d[x] = s[x] | 0xFF000000u;
        }
        fatto |= r;
    }
    if (g_racconta) {
        const QPoint c(m_immagine.width() / 2, m_immagine.height() / 2);
        fprintf(stderr, "exwin: flush %d,%d %dx%d (zona %dx%d, immagine %dx%d) centro %08x\n",
                fatto.x(), fatto.y(), fatto.width(), fatto.height(), z.width(), z.height(),
                m_immagine.width(), m_immagine.height(),
                m_immagine.isNull() ? 0u : m_immagine.pixel(c));
    }
    if (!fatto.isEmpty())
        w->aggiorna(fatto);
}

QT_END_NAMESPACE
