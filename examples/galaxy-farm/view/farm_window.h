#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class GalaxyGlWidget;

namespace app {

class JobRunner;
class JumpPresenter;

// Owns every widget; presenters write through the non-owning pointers below.
class FarmWindow : public QWidget {
public:
    explicit FarmWindow(QWidget* parent = nullptr);

    GalaxyGlWidget* canvas() const { return canvas_; }
    QVBoxLayout* laneColumn() const { return laneColumn_; }
    QProgressBar* progressBar() const { return progressBar_; }
    QLabel* runningLabel() const { return runningLabel_; }
    QLabel* queuedLabel() const { return queuedLabel_; }
    QLabel* generationLabel() const { return generationLabel_; }
    QLabel* staleLabel() const { return staleLabel_; }

    // Set before connectActions to change the initial jump.
    QLineEdit* seedEdit() const { return seedEdit_; }
    QSpinBox* galaxiesPerSectorSpin() const { return galaxiesPerSectorSpin_; }
    QSpinBox* starsPerGalaxySpin() const { return starsPerGalaxySpin_; }
    QSpinBox* maxThreadsSpin() const { return maxThreadsSpin_; }

    // Wires the controls, then fires one initial jump.
    void connectActions(JumpPresenter& presenter, JobRunner& runner);

private:
    QLineEdit* seedEdit_ = nullptr;
    QSpinBox* galaxiesPerSectorSpin_ = nullptr;
    QSpinBox* starsPerGalaxySpin_ = nullptr;
    QPushButton* jumpButton_ = nullptr;
    QSpinBox* maxThreadsSpin_ = nullptr;

    QLabel* cameraLabel_ = nullptr;  // camera readout (polled)

    GalaxyGlWidget* canvas_ = nullptr;
    QVBoxLayout* laneColumn_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* runningLabel_ = nullptr;
    QLabel* queuedLabel_ = nullptr;
    QLabel* generationLabel_ = nullptr;
    QLabel* staleLabel_ = nullptr;
};

}  // namespace app
