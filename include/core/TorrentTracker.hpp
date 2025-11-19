#pragma once

#include <string>
#include "core/TorrentFile.hpp"
#include "net/Peer.hpp"

class TorrentTracker {
public:
    TorrentTracker(const std::string& url);
    void UpdatePeers(const TorrentFile& tf, std::string peer_id, int port);
    const std::vector<Peer>& GetPeers() const;

private:
    std::string url;
    std::vector<Peer> peers;

    void ParseTrackerResponse(const std::string& response);
    void ParseCompactPeers(const std::string& peers_data);
};
