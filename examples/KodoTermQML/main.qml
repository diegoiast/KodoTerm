import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KodoTerm 1.0

ApplicationWindow {
    visible: true
    width: 800
    height: 600
    title: "KodoTerm QML Native Widget"
    color: "#222"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 5

        Label {
            text: "KodoTerm Native QML Item"
            font.pixelSize: 18
            color: "#eee"
            Layout.alignment: Qt.AlignHCenter
        }

        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "black"
                clip: true // Important for QQuickPaintedItem

                KodoQuickTerm {
                    id: terminal
                    anchors.fill: parent
                    focus: true // Request focus on start
                    
                    program: "/bin/bash"
                    // Optional: arguments: ["-l"]
                    
                    Component.onCompleted: {
                        terminal.start()
                    }
                }
            }

            ScrollBar {
                id: vbar
                Layout.fillHeight: true
                orientation: Qt.Vertical
                
                // range: 0 to scrollMax + 1 (for the screen page)
                // Assume 10 lines page size for scrollbar handle size
                
                size: 10 / (terminal.scrollMax + 10)
                position: terminal.scrollValue / (terminal.scrollMax + 10)
                
                active: true
                
                onPositionChanged: {
                    if (pressed) {
                        terminal.scrollValue = Math.round(position * (terminal.scrollMax + 10))
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            
            Button {
                text: "Kill"
                onClicked: terminal.kill()
            }
            
            Button {
                text: "Restart"
                onClicked: terminal.start()
            }

            Label {
                text: "Scroll: " + terminal.scrollValue + "/" + terminal.scrollMax
                color: "#aaa"
            }
        }
    }
}