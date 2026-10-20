// Runs a callable on the Qt GUI thread and returns its result (the automation handlers run on HTTP threads).
#pragma once
#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <optional>
#include <type_traits>
#include <utility>

template <class F>
auto OnGui(F&& f) -> decltype(f())
{
    using R = decltype(f());
    if (QThread::currentThread() == QCoreApplication::instance()->thread())
        return f();
    if constexpr (std::is_void_v<R>)
    {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [&] { f(); }, Qt::BlockingQueuedConnection);
    }
    else
    {
        std::optional<R> r;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [&] { r.emplace(f()); }, Qt::BlockingQueuedConnection);
        return std::move(*r);
    }
}
