/* =============================================================================
 * tools/qt5-exos/ciaoqt.cpp - a Qt Widgets program for tools/qt5-exos/prova.sh
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A main window at a fixed place (100,100), 400x200, so that the test knows
 * where to click without reading Qt's text: a menu bar 24 pixels high with
 * "File > Esci", a line edit at (20,20) 200x28 and a button at (20,80)
 * 120x32 below it. On the screen (the window's client area starts at
 * 100,100, see WinCrea): File ~ (115,112), the line edit (220,158), the
 * button (180,220), "Esci" in the open menu ~ (130,138). Every step it
 * sees is written on stdout as QT-<what>, which is the serial line in the
 * test. QT-ANIMA: a QPropertyAnimation on a dynamic property, 0 to 1 in
 * 300 ms, read after a second (VLC fades its cone in like this): 1 when Qt's
 * animation timer runs. QT-PNG: a 16x8 picture written as PNG in memory and
 * read back (libpng of the sysroot; VLC's cone is a PNG), with the formats
 * QImageReader knows. QT-PNGFILE: an RGBA PNG made by another program
 * ($HOME/prova.png), read from the disk as VLC reads its cone. QT-NATIVO:
 * a child widget finds the window it is flushed to (nativeParentWidget);
 * with winId() 0 it found none, and what it repainted alone never reached
 * the screen (VLC's cone, 19 November 2026).
 * ============================================================================= */
#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemWatcher>
#include <QNetworkInterface>
#include <QSharedMemory>
#include <QSystemSemaphore>
#include <QImage>
#include <QImageReader>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSocketNotifier>
#include <QTimer>
#include <cstdio>
#include <cstring>
#include <sys/inotify.h>
#include <unistd.h>

static void di(const char *cosa, const QString &piu = QString())
{
    std::printf("QT-%s %s\n", cosa, piu.toLocal8Bit().constData());
    std::fflush(stdout);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QMainWindow w;
    w.setWindowTitle(QStringLiteral("Ciao Qt"));
    w.menuBar()->setFixedHeight(24);       /* the clicks of the test count on it */
    QMenu *file = w.menuBar()->addMenu(QStringLiteral("File"));
    QObject::connect(file, &QMenu::aboutToShow, [] { di("MENU"); });
    QAction *esci = file->addAction(QStringLiteral("Esci"));
    QObject::connect(esci, &QAction::triggered, [&app] { di("ESCI"); app.quit(); });

    QWidget *centro = new QWidget;
    QLineEdit *riga = new QLineEdit(centro);
    riga->setGeometry(20, 20, 200, 28);
    QObject::connect(riga, &QLineEdit::returnPressed, [riga] { di("TESTO", riga->text()); });
    QPushButton *bottone = new QPushButton(QStringLiteral("Premi"), centro);
    bottone->setGeometry(20, 80, 120, 32);
    QObject::connect(bottone, &QPushButton::clicked, [] { di("CLIC"); });
    w.setCentralWidget(centro);

    {
        QImage a(16, 8, QImage::Format_RGB32);
        a.fill(Qt::red);
        QByteArray dati;
        QBuffer buf(&dati);
        buf.open(QIODevice::WriteOnly);
        const bool scritto = a.save(&buf, "PNG");
        QImage b;
        const bool letto = b.loadFromData(dati, "PNG");
        QStringList formati;
        for (const QByteArray &f : QImageReader::supportedImageFormats()) formati << QString::fromLatin1(f);
        di("PNG", QStringLiteral("scritto %1 letto %2 %3x%4 byte %5 formati %6")
                      .arg(scritto).arg(letto).arg(b.width()).arg(b.height()).arg(dati.size())
                      .arg(formati.join(QLatin1Char(','))));
    }
    {
        QImageReader r(QString::fromLocal8Bit(qgetenv("HOME")) + QStringLiteral("/prova.png"));
        const QImage c = r.read();
        di("PNGFILE", QStringLiteral("%1x%2 formato %3 errore '%4'")
                          .arg(c.width()).arg(c.height()).arg(int(c.format())).arg(r.errorString()));
    }
    /* (kernel 0.373, voce 85) what Qt asks of the system: semaphores and
     * shared memory between processes (POSIX IPC, sem_open and shm_open),
     * inotify, getifaddrs. QT-IPC, QT-GUARDA, QT-RETE. */
#if QT_CONFIG(systemsemaphore) && QT_CONFIG(sharedmemory)
    {
        QSystemSemaphore s1(QStringLiteral("ciaoqt"), 0, QSystemSemaphore::Create);
        QSystemSemaphore s2(QStringLiteral("ciaoqt"), 0, QSystemSemaphore::Open);
        const bool rel = s2.release(), acq = s1.acquire();
        QSharedMemory m1(QStringLiteral("ciaoqt")), m2(QStringLiteral("ciaoqt"));
        const bool crea = m1.create(4096);
        if (crea) std::memcpy(m1.data(), "ciao", 5);
        const bool att = m2.attach();
        const QString letto = att ? QString::fromLatin1((const char *)m2.constData()) : QString();
        di("IPC", QStringLiteral("sem %1%2 shm %3%4 %5 '%6'").arg(rel).arg(acq).arg(crea).arg(att)
                      .arg(letto).arg(s1.errorString() + m1.errorString() + m2.errorString()));
    }
#else
    di("IPC", QStringLiteral("spento"));
#endif
    QFileSystemWatcher *guarda = new QFileSystemWatcher(&w);
    const QString casa = QString::fromLocal8Bit(qgetenv("HOME"));
    QElapsedTimer *tempo = new QElapsedTimer;
    int *quante = new int(0);
    static qint64 scrittura = -1;
    static qint64 grezzo = -1;
    guarda->addPath(casa);
    QObject::connect(guarda, &QFileSystemWatcher::directoryChanged, [tempo, quante, guarda](const QString &) {
        if (!tempo->isValid() || ++*quante > 1) return;
        QStringList motori;   /* Qt's engine is a child of the watcher */
        for (QObject *o : guarda->children()) motori << QString::fromLatin1(o->metaObject()->className());
        di("GUARDA", QStringLiteral("%1 ms scrittura %2 ms motori %3 grezzo %4 ms")
                         .arg(tempo->elapsed()).arg(scrittura).arg(motori.join(QLatin1Char(',')))
                         .arg(grezzo));
    });
    /* the same folder through a bare inotify fd and a QSocketNotifier: tells
     * the kernel's wake-up apart from Qt's engine */
    {
        const int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (fd >= 0 && inotify_add_watch(fd, casa.toLocal8Bit().constData(), IN_CREATE | IN_MODIFY) >= 0) {
            QSocketNotifier *n = new QSocketNotifier(fd, QSocketNotifier::Read, &w);
            QObject::connect(n, &QSocketNotifier::activated, [fd, tempo] {
                char buf[4096];
                while (read(fd, buf, sizeof buf) > 0) {}
                if (grezzo < 0 && tempo->isValid()) grezzo = tempo->elapsed();
            });
        }
    }
    QTimer::singleShot(1500, [casa, tempo] {
        QFile f(casa + QStringLiteral("/guardato.txt"));
        tempo->start();
        if (f.open(QIODevice::WriteOnly)) { f.write("x"); f.close(); }
        scrittura = tempo->elapsed();
    });
    {
        QStringList nomi;
        for (const QNetworkInterface &i : QNetworkInterface::allInterfaces())
            nomi << i.name() + QLatin1Char('/') + QString::number(i.index());
        di("RETE", nomi.join(QLatin1Char(' ')));
    }
    w.setProperty("opacita", 0.0);
    QPropertyAnimation *anima = new QPropertyAnimation(&w, "opacita", &w);
    anima->setDuration(300);
    anima->setStartValue(0.0);
    anima->setEndValue(1.0);
    anima->start();
    QTimer::singleShot(1000, [&w] { di("ANIMA", QString::number(w.property("opacita").toDouble())); });

    w.setGeometry(100, 100, 400, 200);
    w.show();
    QTimer::singleShot(500, [&w, riga, bottone] {
        /* where things are on the screen, for whoever reads the serial line */
        const QPoint r = riga->mapToGlobal(riga->rect().center());
        const QPoint b = bottone->mapToGlobal(bottone->rect().center());
        di("PRONTO", QStringLiteral("riga %1,%2 bottone %3,%4 menu %5")
                         .arg(r.x()).arg(r.y()).arg(b.x()).arg(b.y())
                         .arg(w.menuBar()->height()));
        di("NATIVO", QStringLiteral("%1 winId %2").arg(riga->nativeParentWidget() == &w)
                         .arg(w.winId() != 0));
    });
    return app.exec();
}
