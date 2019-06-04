#pragma once
#include "PendingActions.hpp"

namespace CNCOnlineForwarder::Utility
{
    class PromisedReady
    {
    public:
        PromisedReady() : ready{ false } {}

        void setReady() noexcept { this->ready = true; }
    private:
        friend class PendingActions<PromisedReady>;

        bool isReady() const noexcept { return this->ready; }

        template<typename Action>
        void apply(Action&& action)
        {
            action();
        }

        bool ready;
    };

    using PendingReadyState = PendingActions<PromisedReady>;
}