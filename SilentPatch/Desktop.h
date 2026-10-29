#pragma once

#include <utility>
#include <cstdint>

std::pair<uint32_t, uint32_t> GetDesktopResolution();
int32_t GetAvailableMemory_Fake(uint32_t* totalVRAM, uint32_t* availableVRAM);
