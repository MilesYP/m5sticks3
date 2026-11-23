#pragma once

#include "nec.h"
#include <M5Unified.h>

namespace ir_test {

enum class State { Waiting, Received, Unknown, Error, Stopped };

void drawScreen(M5Canvas& canvas, State state, const nec::Frame& frame,
                const char* error, int battery);

}  // namespace ir_test
