#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ParseUtils
{
	std::optional<int32_t> TryParseInt(const wchar_t* str);
	std::string ParseString(const wchar_t* str);
};
