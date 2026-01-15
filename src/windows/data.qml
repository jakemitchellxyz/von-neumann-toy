import QtQuick
import QtApplicationManager.Application

ApplicationManagerWindow {
    title: "Data"
    active: true
    color: "#101030"
    singleProcess: false
    leftPadding: 10
    rightPadding: 10
    topPadding: 10
    bottomPadding: 10
    x: 0
    y: 0
    width: 400
    height: 300
    minimumWidth: 400
    minimumHeight: 300
    maximumWidth: 800
    maximumHeight: 600

    RowLayout {
        Text {
            text: "Data!"
            color: "white"
        }
        Text {
            text: "Data!"
            color: "white"
        }
        Text {
            text: "Data!"
            color: "white"
        }
    }
}