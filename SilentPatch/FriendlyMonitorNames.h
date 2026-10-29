#pragma once

#include <map>
#include <string>

namespace FriendlyMonitorNames
{
	std::map<std::string, std::string, std::less<>> GetNamesForDevicePaths();
}
