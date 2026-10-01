// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "cli.hpp"
#include <optional>
namespace mona2 {
    int run_application(Options options,std::optional<std::chrono::seconds> validation_duration=std::nullopt);
}
