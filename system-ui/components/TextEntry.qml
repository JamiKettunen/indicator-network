import QtQuick 1.1
import DBusMenu 1.0

BasicItem {
    id: textEntry

    property alias text: textInput.text

    style: TextEntryStyle { }

    // WORKAROUND
    Rectangle {
        id: title
        height: 24
        anchors { left: parent.left; top: parent.top; right: parent.right;  }

        color: "#ded9d3"
        Text {
            text: dbusModel ? dbusModel.title : ""
            color: "#ffffff"
            font { bold: true; pointSize: 10 }
            anchors { fill: parent; verticalCenter: parent.verticalCenter; margins: textEntry.style.margin }
        }
    }

    Rectangle {
        anchors { left: parent.left; top: title.bottom; right: parent.right; bottom: parent.bottom; margins: textEntry.style.margin }
        border.color: textInput.activeFocus ? textEntry.style.selectedBoderColor : textEntry.style.unselectedBoderColor

        TextInput {
            id: textInput

            text: dbusModel ? dbusModel.data : ""
            anchors.fill: parent
            anchors.margins: textEntry.style.margin
            font { family: textEntry.style.fontFamily; pointSize: textEntry.style.fontPointSize; bold: textEntry.style.fontBold }

            onAccepted: {
                if (dbusModel) {
                    dbusModel.control.sendEvent(dbusModel.id, DBusMenuClientControl.TextChanged, text)
                }
            }

            Text {
                text: dbusModel ? dbusModel.title : ""
                visible: textInput.text == "" && !textInput.activeFocus
                anchors.fill: parent
                font { family: textEntry.style.fontFamily; pointSize: textEntry.style.fontPointSize; bold: textEntry.style.fontBold }
            }
        }
    }
}