#pragma once
#include "UiAppHost.h"
#include "util/YbxReadingSummary.h"

// Shared bounded, allocation-free summary drawing for Home and statistics.
void drawYbxReadingPanel(UiAppHost::UiScreen& screen, freeink::ui::Rect bounds, const ybx::ReadingSummary& summary);
