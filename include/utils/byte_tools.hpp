#pragma once

#include <string>
#include <bitset>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

namespace utils {
int BytesToInt(std::string_view bytes);
std::string IntToBytes(int integer);
std::string CalculateSHA1(const std::string& message);
std::string HexEncode(const std::string& input);
std::string Int64ToBytes(uint64_t value);
uint64_t BytesToInt64(const std::string& bytes);
std::string BytesToHex(const std::string& bytes);
}
