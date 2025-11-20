#include "core/TorrentTracker.hpp"
#include "core/UdpTracker.hpp"
#include "utils/BencodeParser.hpp"
#include "utils/byte_tools.hpp"
#include <cpr/cpr.h>
#include <iostream>
#include <regex>

const std::vector<std::string> kBackupUdpTrackers = {
    "udp://tracker.openbittorrent.com:80",
    "udp://tracker.internetwarriors.net:1337",
    "udp://tracker.leechers-paradise.org:6969",
    "udp://tracker.coppersurfer.tk:6969",
    "udp://open.stealth.si:80",
    "udp://exodus.desync.com:6969",
    "udp://tracker.torrent.eu.org:451"
};

TorrentTracker::TorrentTracker(const std::string& url) : tracker_url(url) {}

void TorrentTracker::UpdatePeers(const TorrentFile& torrent_file,
                                const std::string& peer_id,
                                int port) {
    std::cout << "Tracker URL: " << tracker_url << std::endl;

    std::vector<std::string> all_trackers = { tracker_url };

    if (IsUdpTracker()) {
        for (const auto& backup : kBackupUdpTrackers) {
            if (backup != tracker_url) {
                all_trackers.push_back(backup);
            }
        }
    }

    for (size_t i = 0; i < all_trackers.size() && peers.empty(); ++i) {
        const auto& current_tracker = all_trackers[i];

        std::cout << "\n=== Trying tracker " << (i + 1) << "/" << all_trackers.size()
                  << ": " << current_tracker << " ===" << std::endl;

        try {
            if (IsUdpTracker(current_tracker)) {
                UpdatePeersUdp(torrent_file, peer_id, port, current_tracker);
            } else {
                UpdatePeersHttp(torrent_file, peer_id, port, current_tracker);
            }

            if (!peers.empty()) {
                std::cout << "SUCCESS: Got " << peers.size() << " peers from " << current_tracker << std::endl;
                break;
            }

        } catch (const std::exception& e) {
            std::cerr << "Tracker " << current_tracker << " failed: " << e.what() << std::endl;

            if (i < all_trackers.size() - 1) {
                std::cout << "Trying next tracker..." << std::endl;
            }
        }
    }

    if (peers.empty()) {
        throw std::runtime_error("All trackers failed - no peers found");
    }
}

bool TorrentTracker::IsUdpTracker() const {
    return IsUdpTracker(tracker_url);
}

bool TorrentTracker::IsUdpTracker(const std::string& url) const {
    return url.substr(0, 6) == "udp://";
}

std::pair<std::string, int> TorrentTracker::ParseUdpUrl(const std::string& url) {
    std::cout << "Parsing UDP URL: " << url << std::endl;

    if (url.substr(0, 6) != "udp://") {
        throw std::runtime_error("Invalid UDP URL: " + url);
    }

    std::string host_port = url.substr(6);

    size_t slash_pos = host_port.find('/');
    if (slash_pos != std::string::npos) {
        host_port = host_port.substr(0, slash_pos);
    }

    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        std::string host = host_port.substr(0, colon_pos);
        int port = std::stoi(host_port.substr(colon_pos + 1));
        std::cout << "Parsed: host=" << host << ", port=" << port << std::endl;
        return {host, port};
    } else {
        std::cout << "No port specified, using default: 80" << std::endl;
        return {host_port, 80};
    }
}

Peer TorrentTracker::ConvertTrackerPeer(const UdpTracker::TrackerPeer& tracker_peer) {
    Peer peer;

    peer.ip = std::to_string((tracker_peer.ip >> 24) & 0xFF) + "." +
              std::to_string((tracker_peer.ip >> 16) & 0xFF) + "." +
              std::to_string((tracker_peer.ip >> 8) & 0xFF) + "." +
              std::to_string(tracker_peer.ip & 0xFF);

    peer.port = tracker_peer.port;

    return peer;
}

void TorrentTracker::UpdatePeersHttp(const TorrentFile& torrent_file,
                                    const std::string& peer_id,
                                    int port,
                                    const std::string& url) {
    std::cout << "Using HTTP tracker: " << url << std::endl;

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
        cpr::Timeout{10000},
        cpr::ConnectTimeout{5000}
    );

    if (tracker_response.status_code != 200) {
        throw std::runtime_error("HTTP " + std::to_string(tracker_response.status_code) +
                               ": " + tracker_response.error.message);
    }

    ParseTrackerResponse(tracker_response.text, url);
}


void TorrentTracker::UpdatePeersUdp(const TorrentFile& torrent_file,
                                   const std::string& peer_id,
                                   int port,
                                   const std::string& url) {
    std::cout << "Using UDP tracker: " << url << std::endl;

    try {
        auto [host, tracker_port] = ParseUdpUrl(url);

        std::cout << "Creating UDP tracker for " << host << ":" << tracker_port << std::endl;

        UdpTracker udp_tracker(host, tracker_port, 8);

        std::cout << "Sending announce request..." << std::endl;

        auto response = udp_tracker.Announce(
            torrent_file.info_hash,  // info_hash (20 bytes)
            peer_id,                 // peer_id (20 bytes)
            0,                       // downloaded
            torrent_file.length,     // left
            0,                       // uploaded
            2,                       // event: 2 = started
            -1,                      // num_want: -1 = default
            port                     // port
        );

        peers.clear();
        for (const auto& tracker_peer : response.peers) {
            peers.push_back(ConvertTrackerPeer(tracker_peer));
        }

        std::cout << "SUCCESS: Received " << peers.size() << " peers from UDP tracker" << std::endl;
        std::cout << "Interval: " << response.interval << " seconds" << std::endl;
        std::cout << "Leechers: " << response.leechers << ", Seeders: " << response.seeders << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "UDP tracker " << url << " failed: " << e.what() << std::endl;
        throw;
    }
}

void TorrentTracker::ParseTrackerResponse(const std::string& response) {
    ParseTrackerResponse(response, tracker_url);
}

void TorrentTracker::ParseTrackerResponse(const std::string& response, const std::string& url) {
    utils::BencodeParser parser;
    auto parsed = parser.ParseFromString(response);

    std::string peers_data;
    std::string failure_reason;

    for (size_t i = 0; i < parsed.size(); ++i) {
        if (parsed[i] == "peers" && i + 1 < parsed.size()) {
            peers_data = parsed[i + 1];
        }
        if (parsed[i] == "failure reason" && i + 1 < parsed.size()) {
            failure_reason = parsed[i + 1];
        }
        if (parsed[i] == "interval" && i + 1 < parsed.size()) {
            std::cout << "Tracker interval: " << parsed[i + 1] << " seconds" << std::endl;
        }
    }

    if (!failure_reason.empty()) {
        throw std::runtime_error("Tracker failure: " + failure_reason);
    }

    if (peers_data.empty()) {
        throw std::runtime_error("No peers data in tracker response from " + url);
    }

    ParseCompactPeers(peers_data);
}

void TorrentTracker::ParseCompactPeers(const std::string& peers_data) {
    peers.clear();

    if (peers_data.size() % 6 == 0 && !peers_data.empty()) {
        ParseCompactBinaryPeers(peers_data);
    } else {
        ParseDictionaryPeers(peers_data);
    }

    std::cout << "Parsed " << peers.size() << " peers from tracker" << std::endl;
}

void TorrentTracker::ParseCompactBinaryPeers(const std::string& peers_data) {
    const size_t peer_size = 6;
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
}

void TorrentTracker::ParseDictionaryPeers(const std::string& peers_data) {
    static_cast<void>(peers_data);
    throw std::runtime_error("Non-compact peer format not supported. Use compact=1 in tracker request.");
}

const std::vector<Peer>& TorrentTracker::GetPeers() const {
    return peers;
}

std::string TorrentTracker::GetTrackerUrl() const {
    return tracker_url;
}

bool TorrentTracker::IsWorking() const {
    return !peers.empty();
}

void TorrentTracker::PrintStats() const {
    std::cout << "=== TRACKER STATS ===" << std::endl;
    std::cout << "URL: " << tracker_url << std::endl;
    std::cout << "Type: " << (IsUdpTracker() ? "UDP" : "HTTP") << std::endl;
    std::cout << "Peers available: " << peers.size() << std::endl;

    if (!peers.empty()) {
        std::cout << "First 5 peers:" << std::endl;
        for (size_t i = 0; i < std::min(peers.size(), size_t(5)); ++i) {
            std::cout << "  " << peers[i].ip << ":" << peers[i].port << std::endl;
        }
    }
}
