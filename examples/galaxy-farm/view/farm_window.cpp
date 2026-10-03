#include "view/farm_window.h"

#include <cstdint>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QOverload>
#include <QProgressBar>
#include <QFrame>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShortcut>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector3D>

#include "infra/job_runner.h"
#include "view/galaxy_gl_widget.h"
#include "view/jump_presenter.h"

namespace app {

FarmWindow::FarmWindow(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("farmWindow"));
    setWindowTitle(QStringLiteral("Ordo Galaxy Farm"));

    // ---- LEFT: Workers panel --------------------------------------------
    auto* leftPanel = new QWidget;
    leftPanel->setObjectName(QStringLiteral("leftPanel"));
    leftPanel->setFixedWidth(280);
    auto* leftColumn = new QVBoxLayout(leftPanel);

    leftColumn->addWidget(new QLabel(QStringLiteral("Workers")));

    auto* maxThreadsRow = new QHBoxLayout;
    maxThreadsRow->addWidget(new QLabel(QStringLiteral("Max threads:")));
    maxThreadsSpin_ = new QSpinBox;
    maxThreadsSpin_->setRange(1, 32);
    maxThreadsSpin_->setValue(6);
    maxThreadsRow->addWidget(maxThreadsSpin_);
    maxThreadsRow->addStretch();
    leftColumn->addLayout(maxThreadsRow);

    // Scrolls, so the lane count never drives the window height.
    auto* laneHost = new QWidget;
    laneColumn_ = new QVBoxLayout(laneHost);
    laneColumn_->setContentsMargins(0, 0, 0, 0);
    laneColumn_->addStretch();  // keeps cards top-aligned inside the viewport
    auto* laneScroll = new QScrollArea;
    laneScroll->setObjectName(QStringLiteral("laneScroll"));
    laneScroll->setWidget(laneHost);
    laneScroll->setWidgetResizable(true);
    laneScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    laneScroll->setFrameShape(QFrame::NoFrame);
    leftColumn->addWidget(laneScroll, 1);

    // ---- CENTER: toolbar + canvas + status row ---------------------------
    auto* centerColumn = new QVBoxLayout;

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(QStringLiteral("Seed")));
    seedEdit_ = new QLineEdit(QStringLiteral("8842112273027"));
    // A line edit, since seeds exceed QSpinBox's 32-bit range.
    seedEdit_->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^[0-9]{1,20}$")), seedEdit_));
    seedEdit_->setFixedWidth(140);
    toolbar->addWidget(seedEdit_);

    toolbar->addWidget(new QLabel(QStringLiteral("Galaxies / sector:")));
    galaxiesPerSectorSpin_ = new QSpinBox;
    galaxiesPerSectorSpin_->setRange(1, 128);
    galaxiesPerSectorSpin_->setValue(32);
    toolbar->addWidget(galaxiesPerSectorSpin_);

    toolbar->addWidget(new QLabel(QStringLiteral("Stars / galaxy:")));
    starsPerGalaxySpin_ = new QSpinBox;
    starsPerGalaxySpin_->setRange(1000, 1000000);
    starsPerGalaxySpin_->setSingleStep(1000);
    starsPerGalaxySpin_->setValue(60000);
    toolbar->addWidget(starsPerGalaxySpin_);

    toolbar->addStretch();

    // Live camera readout: eye, orbit target, dolly distance.
    cameraLabel_ = new QLabel;
    cameraLabel_->setObjectName(QStringLiteral("cameraLabel"));
    toolbar->addWidget(cameraLabel_);
    toolbar->addSpacing(12);

    jumpButton_ = new QPushButton(QStringLiteral("Jump"));
    jumpButton_->setObjectName(QStringLiteral("jumpButton"));
    // Always enabled: re-jumping mid-transit is allowed.
    toolbar->addWidget(jumpButton_);
    centerColumn->addLayout(toolbar);

    canvas_ = new GalaxyGlWidget;
    canvas_->setMinimumSize(640, 480);
    centerColumn->addWidget(canvas_, /*stretch=*/1);

    auto* statusRow = new QHBoxLayout;
    statusRow->addWidget(new QLabel(QStringLiteral("Progress")));
    progressBar_ = new QProgressBar;
    progressBar_->setFormat(QStringLiteral("%v / %m galaxies"));
    progressBar_->setTextVisible(true);
    statusRow->addWidget(progressBar_, /*stretch=*/1);

    // Counters with an inline-HTML colored dot.
    runningLabel_ = new QLabel(QStringLiteral("<span style='color:#4caf50;'>●</span> running 0"));
    queuedLabel_ = new QLabel(QStringLiteral("<span style='color:#2a7fff;'>●</span> queued 0"));
    generationLabel_ = new QLabel(QStringLiteral("<span style='color:#b073ff;'>●</span> generation 0"));
    staleLabel_ = new QLabel;
    staleLabel_->setObjectName(QStringLiteral("staleLabel"));
    staleLabel_->setVisible(false);  // shown once staleArrivals > 0
    statusRow->addWidget(runningLabel_);
    statusRow->addWidget(queuedLabel_);
    statusRow->addWidget(generationLabel_);
    statusRow->addWidget(staleLabel_);
    centerColumn->addLayout(statusRow);

    auto* root = new QHBoxLayout(this);
    root->addWidget(leftPanel);
    root->addLayout(centerColumn, /*stretch=*/1);

    // Accents on top of the app-wide dark palette.
    setStyleSheet(QStringLiteral(
        "#farmWindow, QWidget#leftPanel { background-color: #141414; }"
        "QFrame#workerLane { background-color: #1e1e1e; border-radius: 6px; padding: 6px; }"
        "QPushButton#jumpButton { background-color: #14232b; color: #22d3ee; border: 2px solid #22d3ee;"
        " border-radius: 4px; padding: 6px 22px; font-weight: 600; }"
        "QPushButton#jumpButton:hover { background-color: #193039; }"
        "QPushButton#jumpButton:pressed { background-color: #0d1a20; }"
        "QProgressBar { border: 1px solid #333333; border-radius: 3px; text-align: center;"
        " background-color: #1e1e1e; }"
        "QProgressBar::chunk { background-color: #2a7fff; border-radius: 3px; }"
        "QLabel#staleLabel { color: #ff9800; }"
        "QLabel#cameraLabel { color: #7a9bb0; font-family: Consolas, monospace; }"));

    // Polled: the canvas has no signals.
    auto* cameraPoll = new QTimer(this);
    QObject::connect(cameraPoll, &QTimer::timeout, this, [this] {
        const QVector3D eye = canvas_->cameraEyePos();
        const QVector3D tgt = canvas_->cameraTargetPos();
        cameraLabel_->setText(QStringLiteral("cam (%1, %2, %3)  tgt (%4, %5, %6)  d %7")
                                   .arg(static_cast<double>(eye.x()), 0, 'f', 1)
                                   .arg(static_cast<double>(eye.y()), 0, 'f', 1)
                                   .arg(static_cast<double>(eye.z()), 0, 'f', 1)
                                   .arg(static_cast<double>(tgt.x()), 0, 'f', 1)
                                   .arg(static_cast<double>(tgt.y()), 0, 'f', 1)
                                   .arg(static_cast<double>(tgt.z()), 0, 'f', 1)
                                   .arg(static_cast<double>(canvas_->cameraDistance()), 0, 'f', 2));
    });
    cameraPoll->start(200);

    // Enter anywhere in the window = Jump.
    for (const QKeySequence& seq : {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)}) {
        auto* shortcut = new QShortcut(seq, this);
        QObject::connect(shortcut, &QShortcut::activated, this, [this] { jumpButton_->click(); });
    }

    resize(1400, 900);
}

void FarmWindow::connectActions(JumpPresenter& presenter, JobRunner& runner) {
    // Pool size is infrastructure, not domain state, so no event.
    presenter.setWorkerLimit(maxThreadsSpin_->value());
    QObject::connect(maxThreadsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
                      [&runner, &presenter](int n) {
                          runner.setMaxThreads(n);
                          presenter.setWorkerLimit(n);
                      });

    auto fireJump = [&presenter, this] {
        presenter.jump(seedEdit_->text().toULongLong(), galaxiesPerSectorSpin_->value(),
                        starsPerGalaxySpin_->value());
    };
    QObject::connect(jumpButton_, &QPushButton::clicked, jumpButton_, fireJump);

    // Open mid-jump rather than over an empty sector.
    fireJump();
}

}  // namespace app
