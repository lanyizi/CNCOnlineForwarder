#pragma once
#include <functional>
#include <string>
#include <string_view>
#include "IOManager.hpp"

namespace CNCOnlineForwarder::Utility
{
    void asyncHttpGet
    (
        const IOManager::ObjectMaker& objectMaker,
        const std::string_view hostName,
        const std::string_view target,
        std::function<void(std::string)> onGet
    );
}
