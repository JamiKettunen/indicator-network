/*
 * Copyright © 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *     Antti Kaijanmäki <antti.kaijanmaki@canonical.com>
 */

#include <QCoreApplication>
#include <QScopedPointer>
#include <QDebug>

//! [include]
#include <connectivityqt/connectivity.h>
using namespace connectivityqt;
//! [include]

static const QMap<Connectivity::Status, QString> STATUS_MAP {
    {Connectivity::Status::Offline, "Offline"},
    {Connectivity::Status::Connecting, "Connecting"},
    {Connectivity::Status::Online, "Online"},
};

int
main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    //! [create networkingstatus]
    QScopedPointer<Connectivity> ns(new Connectivity());
    //! [create networkingstatus]

    //! [status]
    // normal getter
    if (ns->status() == Connectivity::Status::Online)
    {
        qDebug() << "We are online.";
    }

    // Subscribe to system networking changes
    QObject::connect(ns.data(),
                     &Connectivity::statusUpdated,
                     [](Connectivity::Status value)
    {
        qDebug() << "System networking status changed to: " + STATUS_MAP[value];
    });
    //! [status]

    qDebug() << "System networking status: " + STATUS_MAP[ns->status()];

    //! [limitations]
    // normal getter
    if (ns->limitations().isEmpty())
        qDebug() << "No limitations";

    // Subscribe to limitation changes
    QObject::connect(ns.data(),
                     &Connectivity::limitationsUpdated,
                     [&ns](){
        if (ns->limitations().isEmpty())
        {
            qDebug() << "No limitations.";
            return;
        }

        qDebug() << "Limitations:";
        if (ns->limitations().contains(Connectivity::Limitations::Bandwith))
        {
            qDebug() << "    - Bandwith";
        }
    });
    //! [limitations]

    app.exec();

    return 0;
}
