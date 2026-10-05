#include "view/todo_window.h"

#include <memory>

#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVariant>

#include "model/todo.h"
#include "view/todo_view_model.h"

namespace {

// Keeps the overlay sized to the window: QWidget has no auto-resize
// relationship to its parent, so this handles Resize by hand.
class OverlayResizer : public QObject {
public:
    OverlayResizer(QWidget* window, QWidget* overlay) : QObject(window), window_(window), overlay_(overlay) {}

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == window_ && event->type() == QEvent::Resize) {
            overlay_->setGeometry(window_->rect());
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* window_;
    QWidget* overlay_;
};

}  // namespace

namespace app {

TodoWindow::TodoWindow(TodoViewModel* vm, QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("Ordo TodoMVC"));

    auto* toggleAll = new QCheckBox(QStringLiteral("all"));
    auto* input = new QLineEdit;
    input->setPlaceholderText(QStringLiteral("What needs to be done?"));
    auto* topRow = new QHBoxLayout;
    topRow->addWidget(toggleAll);
    topRow->addWidget(input);

    auto* list = new QListWidget;

    auto* countLabel = new QLabel(QStringLiteral("0 items left"));
    auto* allButton = new QPushButton(QStringLiteral("All"));
    auto* activeButton = new QPushButton(QStringLiteral("Active"));
    auto* completedButton = new QPushButton(QStringLiteral("Completed"));
    allButton->setCheckable(true);
    activeButton->setCheckable(true);
    completedButton->setCheckable(true);
    auto* filterGroup = new QButtonGroup(this);
    filterGroup->setExclusive(true);
    filterGroup->addButton(allButton);
    filterGroup->addButton(activeButton);
    filterGroup->addButton(completedButton);
    allButton->setChecked(true);   // matches TodoListAgent's initial filter_

    auto* deleteButton = new QPushButton(QStringLiteral("Delete"));
    auto* clearButton = new QPushButton(QStringLiteral("Clear completed"));

    auto* bottomRow = new QHBoxLayout;
    bottomRow->addWidget(countLabel);
    bottomRow->addWidget(allButton);
    bottomRow->addWidget(activeButton);
    bottomRow->addWidget(completedButton);
    bottomRow->addWidget(deleteButton);
    bottomRow->addWidget(clearButton);

    auto* column = new QVBoxLayout(this);
    column->addLayout(topRow);
    column->addWidget(list);
    column->addLayout(bottomRow);

    // view -> view-model (intent): submit the new-todo field.
    QObject::connect(input, &QLineEdit::returnPressed, input, [input, vm] {
        vm->addTodo(input->text());
        input->clear();
    });

    // Guards itemChanged below while we rebuild the list, so our own edits
    // aren't mistaken for user edits.
    auto rendering = std::make_shared<bool>(false);

    // view-model -> view: rebuild the list whenever the filtered snapshot changes.
    QObject::connect(vm, &app::TodoViewModel::itemsChanged, list, [list, vm, rendering] {
        *rendering = true;
        list->clear();
        for (const auto& item : vm->items()) {
            auto* listItem = new QListWidgetItem(item.title);
            listItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEditable | Qt::ItemIsEnabled |
                                Qt::ItemIsSelectable);
            listItem->setCheckState(item.completed ? Qt::Checked : Qt::Unchecked);
            listItem->setData(Qt::UserRole, QVariant::fromValue(item.id));
            if (item.completed) {
                QFont font = listItem->font();
                font.setStrikeOut(true);
                listItem->setFont(font);
            }
            list->addItem(listItem);
        }
        *rendering = false;
    });

    // view -> view-model (intent): a checkbox flip is a toggle, any other
    // text change is an edit.
    QObject::connect(list, &QListWidget::itemChanged, list, [vm, rendering](QListWidgetItem* item) {
        if (*rendering) {
            return;
        }
        const quint64 id = item->data(Qt::UserRole).value<quint64>();
        for (const auto& known : vm->items()) {
            if (known.id != id) {
                continue;
            }
            const bool checkedNow = (item->checkState() == Qt::Checked);
            if (checkedNow != known.completed) {
                vm->toggleTodo(id);
            } else if (item->text() != known.title) {
                vm->editTodo(id, item->text());
            }
            break;
        }
    });

    QObject::connect(deleteButton, &QPushButton::clicked, list, [list, vm] {
        auto* current = list->currentItem();
        if (!current) {
            return;
        }
        vm->destroyTodo(current->data(Qt::UserRole).value<quint64>());
    });

    // view-model -> view: counts drive the label, the clear-completed
    // button's enabled state, and the toggle-all checkbox's checked
    // state (blocked while we set it, so it doesn't re-fire toggleAll).
    QObject::connect(vm, &app::TodoViewModel::countsChanged, countLabel,
                      [countLabel, clearButton, toggleAll, vm](int active, int /*completed*/, int /*total*/) {
                          countLabel->setText(QStringLiteral("%1 items left").arg(active));
                          clearButton->setEnabled(vm->hasCompleted());
                          const QSignalBlocker blocker(toggleAll);
                          toggleAll->setChecked(vm->allComplete());
                      });

    QObject::connect(clearButton, &QPushButton::clicked, vm, &app::TodoViewModel::clearCompleted);
    QObject::connect(toggleAll, &QCheckBox::toggled, vm, &app::TodoViewModel::toggleAll);

    QObject::connect(allButton, &QPushButton::clicked, vm, [vm] { vm->setFilter(app::events::Filter::All); });
    QObject::connect(activeButton, &QPushButton::clicked, vm,
                      [vm] { vm->setFilter(app::events::Filter::Active); });
    QObject::connect(completedButton, &QPushButton::clicked, vm,
                      [vm] { vm->setFilter(app::events::Filter::Completed); });

    QObject::connect(vm, &app::TodoViewModel::filterChanged, allButton,
                      [allButton, activeButton, completedButton](app::events::Filter filter) {
                          allButton->setChecked(filter == app::events::Filter::All);
                          activeButton->setChecked(filter == app::events::Filter::Active);
                          completedButton->setChecked(filter == app::events::Filter::Completed);
                      });

    // Saving indicator + persist-error label, next to countLabel:
    // pendingWrites() is the only user-visible trace that a write is in
    // flight -- the UI never waits on one, it just reports it.
    auto* savingLabel = new QLabel(QStringLiteral("saved ✓"));
    savingLabel->setStyleSheet(QStringLiteral("color: #9e9e9e;"));
    QObject::connect(vm, &app::TodoViewModel::pendingWritesChanged, savingLabel, [savingLabel](int pending) {
        savingLabel->setText(pending > 0 ? QStringLiteral("saving…") : QStringLiteral("saved ✓"));
    });

    auto* persistErrorLabel = new QLabel;
    persistErrorLabel->setStyleSheet(QStringLiteral("color: #c62828;"));
    persistErrorLabel->setVisible(false);
    QObject::connect(vm, &app::TodoViewModel::persistErrorChanged, persistErrorLabel,
                      [persistErrorLabel](const QString& error) {
                          persistErrorLabel->setText(error);
                          persistErrorLabel->setVisible(!error.isEmpty());
                      });

    // Inserted right after countLabel: both report ambient state, not actions.
    bottomRow->insertWidget(1, savingLabel);
    bottomRow->insertWidget(2, persistErrorLabel);

    // Loading overlay: covers the window until the first TodosLoaded, since
    // the queued reply only runs once the event loop starts pumping.
    // Blocks input only; the busy bar keeps animating.
    auto* overlay = new QWidget(this);
    // Without this, QWidget paints no background and the stylesheet's
    // `background:` below is silently ignored.
    overlay->setAttribute(Qt::WA_StyledBackground, true);
    overlay->setStyleSheet(QStringLiteral("background: rgba(20, 22, 26, 170);"));

    auto* loadingLabel = new QLabel(QStringLiteral("Loading todos…"));
    loadingLabel->setStyleSheet(QStringLiteral("color: #f0f0f0; font-size: 16px; background: transparent;"));

    // Qt Widgets ships no circular spinner; an indeterminate range busy bar
    // substitutes.
    auto* busyBar = new QProgressBar;
    busyBar->setRange(0, 0);
    busyBar->setFixedWidth(220);
    busyBar->setTextVisible(false);

    auto* overlayLayout = new QVBoxLayout(overlay);
    overlayLayout->addStretch();
    overlayLayout->addWidget(loadingLabel, 0, Qt::AlignCenter);
    overlayLayout->addWidget(busyBar, 0, Qt::AlignCenter);
    overlayLayout->addStretch();

    // The overlay sits on top and swallows mouse events.
    overlay->raise();

    // Always covers the whole window, including later resizes (OverlayResizer).
    // Parented to the window, so it is destroyed alongside it.
    installEventFilter(new OverlayResizer(this, overlay));

    overlay->setVisible(!vm->loaded());
    // Comes down on the first TodosLoaded, success or failure alike; a load
    // error surfaces separately via persistErrorLabel.
    QObject::connect(vm, &app::TodoViewModel::loadedChanged, overlay,
                      [overlay](bool loaded) { overlay->setVisible(!loaded); });

    resize(420, 480);
    // Set right before show(): rect() now reflects the resize() above;
    // OverlayResizer handles every resize after this.
    overlay->setGeometry(rect());
}

}  // namespace app
