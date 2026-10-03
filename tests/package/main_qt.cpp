// Consumer smoke test for ordo::qt, reached only through find_package(ordo
// CONFIG). Wires a Presenter through a real ViewHost/Kernel pair and
// checks its handler ran. QCoreApplication is enough; ordo::qt needs no GUI app.
#include <ordo/qt/presenter.h>
#include <ordo/qt/view_host.h>

#include <ordo/core/kernel.h>
#include <ordo/core/version.h>

#include <cstdio>
#include <string_view>

#include <QCoreApplication>
#include <QString>

namespace {

using ordo::core::Kernel;
using ordo::qt::Presenter;
using ordo::qt::ViewHost;

struct PingEvent {
    static constexpr std::string_view eventName = "Ping";
    int value = 0;
};

// No Q_OBJECT needed: adds no new signal/slot/Q_PROPERTY of its own.
class RecordingPresenter : public Presenter {
public:
    explicit RecordingPresenter(const QString& name) : Presenter(name) {}

    void onRegister() override { subscribe<PingEvent>(&RecordingPresenter::onPing); }

    int receivedValue = 0;

private:
    void onPing(const PingEvent& event) { receivedValue = event.value; }
};

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    std::printf("ordo::core::versionString() = %s\n", ordo::core::versionString());

    Kernel kernel;
    ViewHost host(kernel);
    auto* presenter = host.add<RecordingPresenter>(QStringLiteral("Recorder"));

    kernel.dispatcher().dispatch(PingEvent{42});

    if (presenter->receivedValue != 42) {
        std::fprintf(stderr, "FAIL: expected receivedValue == 42, got %d\n", presenter->receivedValue);
        return 1;
    }

    std::printf("PASS: smoke_qt (ordo::qt via find_package)\n");
    return 0;
}
