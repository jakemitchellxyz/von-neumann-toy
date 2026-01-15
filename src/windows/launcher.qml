import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtApplicationManager.SystemUI

Window {
    width: 800
    height: 600
    visible: true
    color: "#101030"
    title: "Von Neumann Toy Launcher"

    Column {
        spacing: 20
        Repeater {
            model: ApplicationManager
            Column {
                id: delegate
                required property bool isRunning
                required property var application
                required property string name
                Button {
                    text: delegate.name
                    color: "white"
                    onClicked: delegate.isRunning ? delegate.application.stop()
                                                  : delegate.application.start()
                }
            }
        }
    }

    Column {
        anchors.right: parent.right
        Repeater {
            model: WindowManager
            WindowItem {
                required property var model
                width: 600
                height: 200
                window: model.window
            }
        }
    }
}