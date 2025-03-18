#pragma once

#include "Config.h"
#include "MiningThreadData.h"
#include <string>
#include <vector>
#include <chrono>

namespace MiningStats {

void initializeStats(const Config& config);
void updateThreadStats(MiningThreadData* threadData);
void globalStatsMonitor();
void stopStatsMonitor();

} // namespace MiningStats 