#include "view/farm_window.h"

#include <cstdint>

#include <QHBoxLayout>
#include <QLabel>
#include <QOverload>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "infra/job_runner.h"
#include "view/shelf_gl_widget.h"
#include "view/shelf_presenter.h"

namespace app {

FarmWindow::FarmWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("Ordo Mesh Farm"));

    // ---- LEFT: worker panel --------------------------------------------
    auto* leftPanel = new QWidget;
    leftPanel->setFixedWidth(230);
    auto* leftColumn = new QVBoxLayout(leftPanel);

    leftColumn->addWidget(new QLabel(QStringLiteral("Workers")));

    auto* maxThreadsRow = new QHBoxLayout;
    maxThreadsRow->addWidget(new QLabel(QStringLiteral("Max threads")));
    maxThreadsSpin_ = new QSpinBox;
    maxThreadsSpin_->setRange(1, 16);
    maxThreadsSpin_->setValue(4);
    maxThreadsRow->addWidget(maxThreadsSpin_);
    leftColumn->addLayout(maxThreadsRow);

    laneColumn_ = new QVBoxLayout;
    leftColumn->addLayout(laneColumn_);
    leftColumn->addStretch();

    // ---- CENTER: toolbar + shelf + status row --------------------------
    auto* centerColumn = new QVBoxLayout;

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(QStringLiteral("Seed")));
    seedSpin_ = new QSpinBox;
    seedSpin_->setRange(0, 2000000000);
    seedSpin_->setValue(42);
    toolbar->addWidget(seedSpin_);

    toolbar->addWidget(new QLabel(QStringLiteral("Parts")));
    partsSpin_ = new QSpinBox;
    partsSpin_->setRange(1, 64);
    partsSpin_->setValue(16);
    toolbar->addWidget(partsSpin_);

    toolbar->addWidget(new QLabel(QStringLiteral("AO rays")));
    raysSpin_ = new QSpinBox;
    raysSpin_->setRange(1, 1024);
    raysSpin_->setValue(64);
    toolbar->addWidget(raysSpin_);

    regenerateButton_ = new QPushButton(QStringLiteral("Regenerate"));
    toolbar->addWidget(regenerateButton_);
    toolbar->addStretch();
    centerColumn->addLayout(toolbar);

    canvas_ = new ShelfGlWidget;
    canvas_->setMinimumSize(640, 480);
    centerColumn->addWidget(canvas_, /*stretch=*/1);

    progressText_ = new QLabel(QStringLiteral("0 / 0 parts"));
    progressBar_ = new QProgressBar;
    statsLabel_ = new QLabel;
    auto* statusRow = new QHBoxLayout;
    statusRow->addWidget(progressText_);
    statusRow->addWidget(progressBar_, /*stretch=*/1);
    statusRow->addWidget(statsLabel_);
    centerColumn->addLayout(statusRow);

    auto* root = new QHBoxLayout(this);
    root->addWidget(leftPanel);
    root->addLayout(centerColumn, /*stretch=*/1);

    resize(1100, 720);
}

void FarmWindow::connectActions(ShelfPresenter& presenter, JobRunner& runner) {
    // Wired straight to JobRunner, not through an event: thread-pool size
    // is view-side infrastructure, not domain state.
    presenter.setWorkerLimit(maxThreadsSpin_->value());
    QObject::connect(maxThreadsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
                      [&runner, &presenter](int n) {
                          runner.setMaxThreads(n);
                          presenter.setWorkerLimit(n);
                      });

    // view -> presenter (intent): Regenerate button and the initial batch
    // below both funnel through here. smoothIters is fixed at 8 in the GUI.
    auto fireRegenerate = [&presenter, this] {
        presenter.regenerate(static_cast<std::uint64_t>(seedSpin_->value()), partsSpin_->value(), 8,
                              raysSpin_->value());
    };
    QObject::connect(regenerateButton_, &QPushButton::clicked, regenerateButton_, fireRegenerate);

    // Fire one batch before the window is shown.
    fireRegenerate();
}

}  // namespace app
