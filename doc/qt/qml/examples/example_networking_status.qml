/*
 * Copyright (C) 2014 Canonical Ltd.
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
 */


import QtQuick 2.0
import Ubuntu.Components 0.1
import Ubuntu.Connectivity 1.0

MainView {
    id: root
    objectName: "mainView"
    applicationName: "NetworkingStatus"

    width: units.gu(100)
    height: units.gu(75)

    property real margins: units.gu(2)
    property real buttonWidth: units.gu(9)

    // create the NetworkingStatus object
    NetworkingStatus {
        id: networkingStatus

        // full status can be retrieved from the base C++ class
        // status property
        onStatusChanged: {
            if (status === NetworkingStatus.Offline)
                console.log("Status: Offline")
            if (status === NetworkingStatus.Connecting)
                console.log("Status: Connecting")
            if (status === NetworkingStatus.Online)
                console.log("Status: Online")
        }
    }

    Page {
        title: i18n.tr("Networking Status")

        Column {
            anchors.centerIn: parent
            Label {
                // use the online property
                text: networkingStatus.online ? "Online" : "Not online"
                fontSize: "large"
            }
            Label {
                // use the limitedBandwith property
                text: networkingStatus.limitedBandwith ? "Bandwith limited" : "Bandwith not limited"
                fontSize: "large"
            }
        }
    }
}
