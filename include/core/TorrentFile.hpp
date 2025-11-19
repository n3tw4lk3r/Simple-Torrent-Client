#pragma once

#include <string>
#include <vector>

struct TorrentFile {
    std::string announce;
    std::string comment;
    std::vector<std::string> piece_hashes;
    size_t piece_length;
    size_t length;
    std::string name;
    std::string info_hash;
};

TorrentFile LoadTorrentFile(const std::string& filename);
