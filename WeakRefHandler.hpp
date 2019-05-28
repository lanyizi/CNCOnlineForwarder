#pragma once
#include <utility>
#include <memory>
#include "Logging.h"

namespace CNCOnlineForwarder::Utility
{
    template<typename Type, typename Handler>
    class WeakRefHandler
    {
    public:
        WeakRefHandler(const std::weak_ptr<Type>& ref, Handler handler) :
            ref{ ref },
            handler{ std::move(handler) } 
        {}

        template<typename... Arguments>
        void operator()(Arguments&&... arguments)
        {
            using namespace Logging;

            const auto self = ref.lock();
            if (!self)
            {
                logLine<Type>(Level::error, "Tried to execute deferred action after self is died");
                return;
            }
            handler(*self, std::forward<Arguments>(arguments)...);
        }

        // Allow accessing handler members
        Handler* operator->()
        {
            return &this->handler;
        }

    private:
        std::weak_ptr<Type> ref;
        Handler handler;
    };

    template<typename T, typename Handler>
    WeakRefHandler<T, Handler> makeWeakHandler(T* pointer, Handler handler)
    {
        return { pointer->weak_from_this(), std::move(handler) };
    }
}