import QtQuick
import QtQuick.Controls
import QtApplicationManager.Application

ApplicationManagerWindow {
    title: "Controls"
    active: true
    color: "#101030"
    singleProcess: false
    leftPadding: 10
    rightPadding: 10
    topPadding: 10
    bottomPadding: 10
    x: 410
    y: 0
    width: 400
    height: 300
    minimumWidth: 400
    minimumHeight: 300
    maximumWidth: 800
    maximumHeight: 600

    TabBar {
        id: bar
        width: parent.width
        TabButton {
            text: qsTr("Asteroids")
            width: implicitWidth
        }
        TabButton {
            text: qsTr("Compounds")
            width: implicitWidth
        }
        TabButton {
            text: qsTr("Elements")
            width: implicitWidth
        }
    }

    SwipeView {
        width: parent.width
        currentIndex: bar.currentIndex
        Item {
            id: asteroidsTab

            ScrollView {
                width: parent.width
                height: parent.height

                Frame {
                    ColumnLayout {
                        spacing: 10

                        Text {
                            text: "Asteroids!"
                            color: "white"
                        }
                        Text {
                            text: "Asteroids!"
                            color: "white"
                        }
                        Text {
                            text: "Asteroids!"
                            color: "white"
                        }
                    }
                }
            }
        }
        Item {
            id: compoundsTab
            ScrollView {
                width: parent.width
                height: parent.height

                Frame {
                    ColumnLayout {
                        spacing: 10

                        Text {
                            text: "Compounds!"
                            color: "white"
                        }
                        Text {
                            text: "Compounds!"
                            color: "white"
                        }
                        Text {
                            text: "Compounds!"
                            color: "white"
                        }
                    }
                }
            }
        }
        Item {
            id: elementsTab

            ScrollView {
                width: parent.width
                height: parent.height

                Frame {
                    ColumnLayout {
                        spacing: 10

                        Text {
                            text: "Elements!"
                            color: "white"
                        }
                        Text {
                            text: "Elements!"
                            color: "white"
                        }
                        Text {
                            text: "Elements!"
                            color: "white"
                        }
                    }
                }
            }
        }
    }
}