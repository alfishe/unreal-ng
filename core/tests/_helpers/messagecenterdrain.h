#pragma once

// DrainMessageCenter() lives in testwaithelper.h (TestWait::ForExactly uses it): everything posted to the default
// MessageCenter before the call has been delivered when it returns. This header keeps the global name for its callers.
#include "_helpers/testwaithelper.h"

using TestWait::DrainMessageCenter;
