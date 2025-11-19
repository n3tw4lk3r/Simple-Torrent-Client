#include "utils/byte_tools.hpp"
#include <cstdio>

int utils::BytesToInt(std::string_view bytes) {
    return int(static_cast<unsigned char>(bytes[0]) << 24 |
               static_cast<unsigned char>(bytes[1]) << 16 |
               static_cast<unsigned char>(bytes[2]) << 8 |
               static_cast<unsigned char>(bytes[3]));
}

std::string utils::IntToBytes(int to_convert) {
    std::string result;
    result += static_cast<unsigned char>(to_convert >> 24) & 0xFF;
    result += static_cast<unsigned char>(to_convert >> 16) & 0xFF;
    result += static_cast<unsigned char>(to_convert >> 8) & 0xFF;
    result += static_cast<unsigned char>(to_convert) & 0xFF;
    return result;
}

std::string utils::CalculateSHA1(const std::string& msg) {
    unsigned char hashed_array[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(msg.data()), msg.size(), hashed_array);
    std::string hashed_string;
    hashed_string.assign(reinterpret_cast<const char*>(hashed_array), SHA_DIGEST_LENGTH);
    return hashed_string;
}

std::string utils::HexEncode(const std::string& input) {
    std::stringstream string_stream;
    string_stream << std::hex;
    for (const char& ch : input) {
        string_stream << std::setw(2) << std::setfill('0') << (int)ch;
    }
    return string_stream.str();
}
