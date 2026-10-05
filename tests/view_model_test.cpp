#include <ordo/qt/view_host.h>
#include <ordo/qt/view_model.h>

#include <ordo/core/kernel.h>

#include <string_view>

#include <QString>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using ordo::qt::ViewHost;
using ordo::qt::ViewModel;

struct TaskAdded {
    static constexpr std::string_view eventName = "TaskAdded";
    int id = 0;
};

// One observable property fed by an event subscription.
class CounterViewModel : public ViewModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    CounterViewModel() : ViewModel(QStringLiteral("CounterViewModel")) {}

    void onRegister() override { subscribe<TaskAdded>(&CounterViewModel::onTaskAdded); }

    int count() const { return count_; }

signals:
    void countChanged(int count);

private:
    void onTaskAdded(const TaskAdded&) {
        ++count_;
        emit countChanged(count_);
    }

    int count_ = 0;
};

TEST(ViewModelTest, EventUpdatesPropertyAndEmitsSignal) {
    Kernel kernel;
    ViewHost host(kernel);
    auto* viewModel = host.add<CounterViewModel>();

    int notified = -1;
    QObject::connect(viewModel, &CounterViewModel::countChanged, [&notified](int c) { notified = c; });

    kernel.send(TaskAdded{1});
    kernel.send(TaskAdded{2});

    EXPECT_EQ(viewModel->count(), 2);
    EXPECT_EQ(notified, 2);
}

TEST(ViewModelTest, DestructionUnsubscribes) {
    Kernel kernel;
    {
        ViewHost host(kernel);
        host.add<CounterViewModel>();
    }
    kernel.send(TaskAdded{1});
    SUCCEED();
}

}  // namespace

#include "view_model_test.moc"
