#include "view/task_list_window.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "viewmodel/task_list_view_model.h"

namespace app {

TaskListWindow::TaskListWindow(TaskListViewModel* viewModel, QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("Ordo task list (MVVM + output port)"));

    auto* input = new QLineEdit;
    input->setPlaceholderText(QStringLiteral("New task title"));
    auto* addButton = new QPushButton(QStringLiteral("Add"));
    auto* errorLabel = new QLabel;
    errorLabel->setStyleSheet(QStringLiteral("color:#c2185b"));
    auto* list = new QListWidget;
    auto* status = new QLabel(QStringLiteral("0 tasks"));

    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(input);
    inputRow->addWidget(addButton);
    auto* column = new QVBoxLayout(this);
    column->addLayout(inputRow);
    column->addWidget(errorLabel);
    column->addWidget(list);
    column->addWidget(status);

    // view -> view-model (intent)
    auto submit = [input, viewModel] { viewModel->addTask(input->text()); };
    QObject::connect(addButton, &QPushButton::clicked, submit);
    QObject::connect(input, &QLineEdit::returnPressed, submit);

    // view-model -> view: broadcast-fed state (1:N)
    QObject::connect(viewModel, &TaskListViewModel::taskAdded, list,
                     [list](const QString& title) { list->addItem(title); });
    QObject::connect(viewModel, &TaskListViewModel::countChanged, status,
                     [status](int count) { status->setText(QStringLiteral("%1 tasks").arg(count)); });

    // view-model -> view: port-fed state (1:1, only this window's concern)
    QObject::connect(viewModel, &TaskListViewModel::lastErrorChanged, errorLabel, &QLabel::setText);
    QObject::connect(viewModel, &TaskListViewModel::inputAccepted, input, [input] {
        input->clear();
        input->setFocus();
    });

    resize(360, 440);
}

}  // namespace app
