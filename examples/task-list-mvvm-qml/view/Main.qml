import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 420
    height: 560
    title: "ordo task list (QML)"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: input
                Layout.fillWidth: true
                placeholderText: "What needs doing?"
                onAccepted: taskListViewModel.addTask(input.text)
            }

            Button {
                text: "Add"
                onClicked: taskListViewModel.addTask(input.text)
            }
        }

        Label {
            text: taskListViewModel.lastError
            visible: text.length > 0
            color: "#c0392b"
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: taskListViewModel.items

            delegate: RowLayout {
                width: ListView.view.width
                spacing: 8

                CheckBox {
                    // A click writes `checked` directly, which would sever a
                    // plain `checked: model.done` binding; the Binding element
                    // keeps re-asserting the projection's value, so the row
                    // can never drift from it.
                    Binding on checked { value: model.done }
                    onToggled: taskListViewModel.toggleTask(model.taskId)
                }

                Label {
                    text: model.title
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.strikeout: model.done
                }

                Button {
                    text: "✕"
                    onClicked: taskListViewModel.removeTask(model.taskId)
                }
            }
        }
    }

    Connections {
        target: taskListViewModel
        function onInputAccepted() {
            input.clear()
            input.forceActiveFocus()
        }
    }
}
