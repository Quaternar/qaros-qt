// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/seam/panel_attach.hpp"

#include <algorithm>
#include <cstring>

namespace qar_qt_streaming::seam
{

namespace
{

bool
contains_peer(const std::vector<PeerId> & list, const PeerId & id)
{
  return std::any_of(list.begin(), list.end(), [&id](const PeerId & other) {
      return std::memcmp(other.data, id.data, sizeof(id.data)) == 0;
    });
}

}  // namespace

PanelAttachDecision
decide_panel_attach(const PanelProbe & probe, unsigned retries_done)
{
  PanelAttachDecision decision;
  if (!probe.found) {
    decision.kind = PanelAttachKind::Created;
    return decision;
  }

  if (probe.content_mismatch) {
    const bool can_retry = retries_done == 0;
    decision.reason = "other content type";
    decision.kind = can_retry ? PanelAttachKind::RetryWithSuffix : PanelAttachKind::Refused;
    if (can_retry) {
      decision.retry_suffix = "-v2";
    }
    return decision;
  }
  if (probe.state == PanelState::Closed) {
    const bool can_retry = retries_done < kMaxClosedPanelRetries;
    decision.reason = "closed";
    decision.kind = can_retry ? PanelAttachKind::RetryWithSuffix : PanelAttachKind::Refused;
    if (can_retry) {
      decision.retry_suffix = "-r" + std::to_string(retries_done + 1);
    }
    return decision;
  }
  if (!probe.content_differs && !probe.size_differs && probe.state == PanelState::Visible) {
    decision.kind = PanelAttachKind::Created;
    return decision;
  }

  decision.kind = PanelAttachKind::Reattached;
  decision.swap_content = probe.content_differs;
  decision.change_size = probe.size_differs;
  decision.show = probe.state == PanelState::Hidden ||
    probe.state == PanelState::Minimized;
  return decision;
}

VisibleToPlan
plan_visible_to(const std::vector<PeerId> & current, const std::vector<PeerId> & wanted)
{
  VisibleToPlan plan;
  for (const PeerId & id : wanted) {
    if (!contains_peer(current, id) && !contains_peer(plan.additions, id)) {
      plan.additions.push_back(id);
    }
  }
  for (const PeerId & id : current) {
    if (!contains_peer(wanted, id) && !contains_peer(plan.removals, id)) {
      plan.removals.push_back(id);
    }
  }
  return plan;
}

std::string
panel_state_name(PanelState state)
{
  switch (state) {
    case PanelState::Visible:
      return "VISIBLE";
    case PanelState::Minimized:
      return "MINIMIZED";
    case PanelState::Hidden:
      return "HIDDEN";
    case PanelState::Closed:
      return "CLOSED";
  }
  return "state " + std::to_string(static_cast<int>(state));
}

std::string
panel_attach_log_line(const PanelAttachLogFields & fields)
{
  return "[qar-panel-adapter] " + fields.time + " panel '" + fields.key + "' " +
         (fields.reattached ? "reattached" : "created") + " name=" + fields.common_name +
         " id=" + fields.panel_id + " state_was=" + fields.state_was +
         " stream_swapped=" + (fields.stream_swapped ? "y" : "n") +
         " size_changed=" + (fields.size_changed ? "y" : "n");
}

}  // namespace qar_qt_streaming::seam
