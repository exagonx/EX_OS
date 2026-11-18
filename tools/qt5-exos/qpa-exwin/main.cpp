/* =============================================================================
 * tools/qt5-exos/qpa-exwin/main.cpp - the "exwin" platform plugin of Qt 5
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-or-later
 * ============================================================================= */
#include <qpa/qplatformintegrationplugin.h>
#include "qexwin.h"

QT_BEGIN_NAMESPACE

class QExwinIntegrationPlugin : public QPlatformIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformIntegrationFactoryInterface_iid FILE "exwin.json")
public:
    QPlatformIntegration *create(const QString &system, const QStringList &param) override
    {
        Q_UNUSED(param);
        if (system.compare(QLatin1String("exwin"), Qt::CaseInsensitive) != 0)
            return nullptr;
        QExwinIntegration *i = new QExwinIntegration;
        if (!i->server()) {         /* no ExWin running: say so, as xcb does */
            delete i;
            return nullptr;
        }
        return i;
    }
};

QT_END_NAMESPACE

#include "main.moc"
