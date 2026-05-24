#pragma once

#include "cget/core/types.h"

namespace cget {

[[nodiscard]] bool isValidTransition(TaskStatus current, TaskStatus next);
void requireValidTransition(TaskStatus current, TaskStatus next);

}  // namespace cget
