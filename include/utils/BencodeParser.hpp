#pragma once

#include <string>
#include <vector>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <iostream>

namespace utils {
class BencodeParser {
private:
    std::string to_decode;
    std::string info_hash;
    std::vector<std::string> parsed;
    std::vector<std::string> pieces_hashes;
    int index;

    std::string ReadFixedAmount(int amount);
    std::string ReadUntilDelimiter(char delimiter);
    std::string Process();
    void ProcessDict();
    void ProcessList();

public:
    BencodeParser();
    ~BencodeParser() = default;

    std::vector<std::string> ParseFromFile(const std::string& filename);
    std::vector<std::string> ParseFromString(std::string str);
    std::string GetHash();
    std::vector<std::string> GetPieceHashes();
};
}
