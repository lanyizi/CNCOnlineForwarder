#pragma once
#ifndef PCH_H
#define PCH_H

#ifdef _WIN32
#include <SDKDDKVer.h>
#endif

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>
#include <utility>

#include <boost/system/error_code.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <boost/asio.hpp>

#include <boost/endian/conversion.hpp>

#include <boost/container_hash/hash.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <boost/algorithm/string/trim.hpp>

#endif