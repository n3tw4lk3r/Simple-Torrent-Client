#pragma once

#include <string>
#include <bitset>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

namespace utils {
int BytesToInt(std::string_view bytes);
std::string IntToBytes(int Int);
std::string CalculateSHA1(const std::string& msg);
std::string HexEncode(const std::string& input);
}
