#include "core/TorrentTracker.hpp"
#include "utils/BencodeParser.hpp"
#include "utils/byte_tools.hpp"
#include <cpr/cpr.h>
#include <iostream>

TorrentTracker::TorrentTracker(const std::string& url) : url(url) {}

void TorrentTracker::UpdatePeers(const TorrentFile& torrent_file, std::string peer_id, int port) {
    std::cout << "Tracker URL: " << url << std::endl;

    try {
        cpr::Response tracker_response = cpr::Get(
            cpr::Url{url},
            cpr::Parameters{
                {"info_hash", torrent_file.info_hash},
                {"peer_id", peer_id},
                {"port", std::to_string(port)},
                {"uploaded", "0"},
                {"downloaded", "0"},
                {"left", std::to_string(torrent_file.length)},
                {"compact", "1"}
            },
            cpr::Timeout{30000},
            cpr::ConnectTimeout{10000}
        );

        if (tracker_response.status_code != 200) {
            throw std::runtime_error("HTTP " + std::to_string(tracker_response.status_code) +
                                   ": " + tracker_response.error.message);
        }

        ParseTrackerResponse(tracker_response.text);

    } catch (const std::exception& e) {
        std::cerr << "Tracker error: " << e.what() << std::endl;
        throw;
    }
}

void TorrentTracker::ParseTrackerResponse(const std::string& response) {
    utils::BencodeParser parser;
    auto parsed = parser.ParseFromString(response);

    std::string peers_data;
    for (size_t i = 0; i < parsed.size(); ++i) {
        if (parsed[i] == "peers" && i + 1 < parsed.size()) {
            peers_data = parsed[i + 1];
            break;
        }
        if (parsed[i] == "failure reason" && i + 1 < parsed.size()) {
            throw std::runtime_error("Tracker failure: " + parsed[i + 1]);
        }
    }

    if (peers_data.empty()) {
        throw std::runtime_error("No peers data in tracker response");
    }

    ParseCompactPeers(peers_data);
}

void TorrentTracker::ParseCompactPeers(const std::string& peers_data) {
    peers.clear();

    const size_t peer_size = 6;
    if (peers_data.size() % peer_size != 0) {
        throw std::runtime_error("Invalid compact peers data format");
    }

    const size_t peerCount = peers_data.size() / peer_size;
    peers.reserve(peerCount);

    for (size_t i = 0; i < peers_data.size(); i += peer_size) {
        std::string ip = std::to_string(static_cast<uint8_t>(peers_data[i])) + "." +
                        std::to_string(static_cast<uint8_t>(peers_data[i + 1])) + "." +
                        std::to_string(static_cast<uint8_t>(peers_data[i + 2])) + "." +
                        std::to_string(static_cast<uint8_t>(peers_data[i + 3]));

        int port = (static_cast<uint8_t>(peers_data[i + 4]) << 8) |
                   static_cast<uint8_t>(peers_data[i + 5]);

        peers.emplace_back(Peer{ip, port});
    }

    std::cout << "Parsed " << peers.size() << " peers from tracker" << std::endl;
}

const std::vector<Peer>& TorrentTracker::GetPeers() const {
    return peers;
}
