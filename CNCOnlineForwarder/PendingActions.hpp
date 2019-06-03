#pragma once
#include <functional>
#include <optional>
#include <vector>
#include <utility>

namespace CNCOnlineForwarder::Utility
{
    template<typename FutureData>
    class PendingActions
    {
    public:
        PendingActions(FutureData data) :
            data{ data },
            pendingActions{ std::in_place }
        {}

        FutureData* operator->() noexcept
        {
            return &this->data;
        }

        void trySetReady()
        {
            this->setReadyIf(this->pendingActions.has_value() && this->data.isReady());
        }

        void setReadyIf(bool condition)
        {
            if (!condition)
            {
                return;
            }

            auto actions = std::move(this->pendingActions.value());
            this->pendingActions.reset();
            for (auto& action : actions)
            {
                this->data.apply(std::move(action));
            }
        }

        template<typename Action>
        void asyncDo(Action&& action)
        {
            if (this->pendingActions.has_value())
            {
                this->pendingActions.emplace_back(std::forward<Action>(action)));
                return;
            }

            this->data.apply(std::forward<Action>(action));
        }

    private:
        FutureData data;
        std::optional<std::vector<std::function<void(FutureData...)>>> pendingActions;
    };
}