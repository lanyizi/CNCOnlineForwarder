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
        template<typename InputHandler>
        WeakRefHandler(const std::weak_ptr<Type>& ref, InputHandler&& handler) :
            ref{ ref },
            handler{ std::forward<InputHandler>(handler) } 
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

            std::invoke(this->handler, *self, std::forward<Arguments>(arguments)...);
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

    namespace Details
    {
        template<typename T>
        struct IsTemplate : std::false_type
        {
            using HeadType = void;
        };

        template<typename Head, typename... Tail, template<typename...> class T>
        struct IsTemplate<T<Head, Tail...>> : std::true_type
        {
            using HeadType = Head;
        };
    }

    template<typename T, typename Handler>
    auto makeWeakHandler
    (
        const T& pointer, 
        Handler&& handler
    )
    {
        using TemplateCheck = Details::IsTemplate<T>;
        using HandlerValue = std::remove_reference_t<Handler>;
        if constexpr (std::conjunction_v<TemplateCheck, std::is_convertible<const T&, std::weak_ptr<typename TemplateCheck::HeadType>>>)
        {
            return WeakRefHandler<typename TemplateCheck::HeadType, HandlerValue>
            { 
                pointer, 
                std::forward<Handler>(handler)
            };
        }
        else
        {
            static_assert(std::is_pointer_v<T>);
            return WeakRefHandler<std::remove_pointer_t<T>, HandlerValue>
            { 
                pointer->weak_from_this(), 
                std::forward<Handler>(handler)
            };
        }
    }
}