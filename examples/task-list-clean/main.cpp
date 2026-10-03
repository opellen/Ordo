#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "entities/task_list.h"
#include "interface_adapters/task_list_presenter.h"
#include "use_cases/add_task_command.h"
#include "use_cases/add_task_request.h"

namespace {

// Headless smoke test: drives both result channels and checks what the
// presenter wrote onto the widgets directly. Blank title exercises
// rejection; valid title exercises broadcast + acceptance echo.
int runSmoke(QLineEdit* input, QLabel* errorLabel, QListWidget* list, QLabel* status,
             app::interface_adapters::TaskListPresenter* presenter) {
    presenter->addTask(QStringLiteral("  "));
    if (errorLabel->text() != QStringLiteral("title is empty") || list->count() != 0) {
        std::fprintf(stderr, "FAIL: rejection not observed via the port (error='%s', count=%d)\n",
                     errorLabel->text().toUtf8().constData(), list->count());
        return 1;
    }

    presenter->addTask(QStringLiteral("ship ordo"));
    if (list->count() != 1 || !errorLabel->text().isEmpty() ||
        status->text() != QStringLiteral("1 tasks") || !input->text().isEmpty()) {
        std::fprintf(stderr, "FAIL: count=%d, error='%s', status='%s', input='%s'\n", list->count(),
                     errorLabel->text().toUtf8().constData(), status->text().toUtf8().constData(),
                     input->text().toUtf8().constData());
        return 1;
    }

    std::printf("PASS: task-list-clean smoke (imperative presenter, both channels)\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);

    // Bootstrap: the only place Kernel appears.
    ordo::core::Kernel kernel;
    kernel.registerAgent(std::make_shared<app::entities::TaskList>());

    // Widgets are built unconditionally; smoke reuses them without calling
    // show(). window must be declared before host: destruction is
    // reverse-declaration order, so the presenter is torn down before the
    // widgets it points to.
    QWidget window;
    window.setWindowTitle(QStringLiteral("Ordo task list (Clean-first layout)"));

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
    auto* column = new QVBoxLayout(&window);
    column->addLayout(inputRow);
    column->addWidget(errorLabel);
    column->addWidget(list);
    column->addWidget(status);

    ordo::qt::ViewHost host(kernel);
    auto* presenter =
        host.add<app::interface_adapters::TaskListPresenter>(input, errorLabel, list, status, &window);

    // The presenter is the command's output port, injected via
    // registerCommand's argument forwarding.
    kernel.registerCommand<app::use_cases::AddTaskRequest, app::use_cases::AddTaskCommand>(
        std::ref(*presenter));

    // view -> presenter (intent)
    auto submit = [input, presenter] { presenter->addTask(input->text()); };
    QObject::connect(addButton, &QPushButton::clicked, submit);
    QObject::connect(input, &QLineEdit::returnPressed, submit);

    int exitCode = 0;
    if (argc > 1 && std::strcmp(argv[1], "--smoke") == 0) {
        exitCode = runSmoke(input, errorLabel, list, status, presenter);
    } else {
        window.resize(360, 440);
        window.show();
        exitCode = application.exec();
    }

    // The presenter (owned by host) must outlive the command registration;
    // remove the registration before it can be destroyed.
    kernel.removeCommand<app::use_cases::AddTaskRequest>();

    return exitCode;
}
