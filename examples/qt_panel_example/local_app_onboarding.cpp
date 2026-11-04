// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "local_app_onboarding.hpp"

#include <utility>

qaros::Result<qaros::Session> onboard_local_app(
  const qaros::Runtime & runtime, qaros::CancelTokenHandle * cancel,
  const std::string & display_name)
{
  QarOnboardLocalAppExt local = qar_onboard_local_app_ext_default();  // the Hub's default wait
  QarOnboardInit init = qar_onboard_init_default();
  init.header.next = &local;
  init.presentation.display_name = display_name.c_str();
  QarOnboardingId onboarding_id = qar_onboarding_id_default();  // stays zero in this mode
  qaros::SessionPtr joined;
  const qaros::Status status(qar_runtime_onboard(
    runtime.Get(), &init, nullptr, nullptr, cancel, &onboarding_id, qaros::OutPtr(joined)));
  if (!status.Ok()) {
    return qaros::Result<qaros::Session>(status);
  }
  return qaros::Session(std::move(joined));
}
