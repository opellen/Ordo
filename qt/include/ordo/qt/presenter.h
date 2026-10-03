#pragma once

#include <ordo/qt/view_adapter.h>

namespace ordo::qt {

// View mediator: reacts to facts by updating its view component imperatively,
// and translates view signals into intents.
class Presenter : public ViewAdapter {
    Q_OBJECT

public:
    // Does not take ownership of viewComponent -- the caller keeps managing its
    // lifetime (typically Qt's parent/child tree owns it separately).
    explicit Presenter(const QString& name, QObject* viewComponent = nullptr);

    QObject* viewComponent() const;
    void setViewComponent(QObject* view);

private:
    QObject* viewComponent_;
};

}  // namespace ordo::qt
