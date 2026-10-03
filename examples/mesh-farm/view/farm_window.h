#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class ShelfGlWidget;

namespace app {

class JobRunner;
class ShelfPresenter;

// The window: every widget the GUI needs, built in the constructor. No
// Q_OBJECT: connectActions wires lambdas/signals directly, no own signals or slots.
class FarmWindow : public QWidget {
public:
    explicit FarmWindow(QWidget* parent = nullptr);

    // The window owns every widget below; a presenter only ever writes
    // through these pointers.
    ShelfGlWidget* canvas() const { return canvas_; }
    QVBoxLayout* laneColumn() const { return laneColumn_; }
    QLabel* progressText() const { return progressText_; }
    QProgressBar* progressBar() const { return progressBar_; }
    QLabel* statsLabel() const { return statsLabel_; }
    QSpinBox* maxThreadsSpin() const { return maxThreadsSpin_; }

    // Wires the Regenerate button and max-threads spinbox to `presenter`
    // and `runner`, then fires one initial batch. Called after the
    // presenter exists, since its constructor needs this window's widgets.
    void connectActions(ShelfPresenter& presenter, JobRunner& runner);

private:
    QSpinBox* maxThreadsSpin_ = nullptr;
    QSpinBox* seedSpin_ = nullptr;
    QSpinBox* partsSpin_ = nullptr;
    QSpinBox* raysSpin_ = nullptr;
    QPushButton* regenerateButton_ = nullptr;

    ShelfGlWidget* canvas_ = nullptr;
    QVBoxLayout* laneColumn_ = nullptr;
    QLabel* progressText_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* statsLabel_ = nullptr;
};

}  // namespace app
