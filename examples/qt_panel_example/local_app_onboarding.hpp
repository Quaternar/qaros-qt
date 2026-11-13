// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QT_PANEL_EXAMPLE__LOCAL_APP_ONBOARDING_HPP_
#define QT_PANEL_EXAMPLE__LOCAL_APP_ONBOARDING_HPP_

// Joining the QAROS Hub of this PC as a local app, without a code. The C++ API of the SDK has no
// builder for this mode, so this function calls the C onboarding API (qar_runtime_onboard with
// QarOnboardLocalAppExt) itself. It is the only code of the example that does.

#include <qar_streaming_cpp/Runtime.hpp>

#include <string>

// Blocks until the Hub has answered, which includes the time the person at the PC takes to allow
// the program on its first start (the Hub waits 120 s by default): call it off the GUI thread.
// @p cancel is handed to the call; it may be null. The SDK documents the effect of a cancel token
// for the asynchronous calls only, so the caller does not rely on it to end this call: a program
// that quits during the approval wait stops waiting for the call and ends without it.
//
// The mode is stateless: nothing is stored, the Hub recognises the program on its next start
// while it matches what was allowed.
qaros::Result<qaros::Session> onboard_local_app(
  const qaros::Runtime & runtime, qaros::CancelTokenHandle * cancel,
  const std::string & display_name);

#endif  // QT_PANEL_EXAMPLE__LOCAL_APP_ONBOARDING_HPP_
