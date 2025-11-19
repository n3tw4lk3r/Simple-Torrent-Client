#include "core/TorrentClient.hpp"
#include "net/PeerConnect.hpp"
#include <iostream>
#include <chrono>
#include <random>
#include <thread>

using namespace std::chrono_literals;

TorrentClient::TorrentClient(const std::string& peer_id)
    : peer_id(peer_id + GenerateRandomSuffix()) {}

std::string TorrentClient::GenerateRandomSuffix(size_t length) {
    static std::random_device random;
    static std::mt19937 gen(random());
    static std::uniform_int_distribution<> distribution('A', 'Z');

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(static_cast<char>(distribution(gen)));
    }
    return result;
}

bool TorrentClient::RunDownloadMultithread(PieceStorage& pieces,
                                           const TorrentFile& torrent_file,
                                           const TorrentTracker& tracker) {
    std::vector<std::thread> peer_threads;
    std::vector<std::shared_ptr<PeerConnect>> peer_connections;

    for (const Peer& peer : tracker.GetPeers()) {
        try {
            peer_connections.emplace_back(
                std::make_shared<PeerConnect>(peer, torrent_file, peer_id, pieces)
            );
        } catch (const std::exception& e) {
            std::cerr << "Failed to create connection to " << peer.ip << ":" << peer.port
                      << " - " << e.what() << std::endl;
        }
    }

    if (peer_connections.empty()) {
        std::cerr << "No valid peer connections established" << std::endl;
        return true;
    }

    peer_threads.reserve(peer_connections.size());
    for (auto& peer_connect_ptr : peer_connections) {
        peer_threads.emplace_back([peer_connect_ptr]() {
            int attempts = 0;
            static constexpr int max_number_of_attempts = 3;
            bool should_try_again = true;

            while (should_try_again && attempts < max_number_of_attempts) {
                try {
                    ++attempts;
                    peer_connect_ptr->Run();
                    should_try_again = false; // Success
                } catch (const std::exception& e) {
                    std::cerr << "Connection attempt " << attempts << " failed: "
                              << e.what() << std::endl;
                    should_try_again = peer_connect_ptr->Failed() && attempts < max_number_of_attempts;
                    if (should_try_again) {
                        std::this_thread::sleep_for(2s * attempts);
                    }
                }
            }
        });
    }

    std::cout << "Started " << peer_threads.size() << " threads for peers" << std::endl;

    const size_t target_pieces = pieces.TotalPiecesCount();
    auto last_progress_time = std::chrono::steady_clock::now();
    size_t last_saved_count = 0;

    while (!is_terminated && pieces.PiecesSavedToDiscCount() < target_pieces) {
        std::this_thread::sleep_for(500ms);

        auto now = std::chrono::steady_clock::now();
        size_t current_saved_count = pieces.PiecesSavedToDiscCount();

        if (current_saved_count == last_saved_count) {
            if (now - last_progress_time > 30s) {
                std::cout << "Download stalled, requesting new peers..." << std::endl;
                break;
            }
        } else {
            last_saved_count = current_saved_count;
            last_progress_time = now;
        }

        if (current_saved_count % 10 == 0 || current_saved_count == target_pieces) {
            std::cout << "Progress: " << current_saved_count << "/" << target_pieces
                      << " pieces (" << (current_saved_count * 100 / target_pieces) << "%)"
                      << std::endl;
        }
    }

    is_terminated = true;
    for (auto& peer_connect_ptr : peer_connections) {
        peer_connect_ptr->Terminate();
    }

    for (auto& thread : peer_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    return pieces.PiecesSavedToDiscCount() < target_pieces;
}

void TorrentClient::DownloadFromTracker(const TorrentFile& torrent_file, PieceStorage& pieces) {
    std::cout << "Connecting to tracker " << torrent_file.announce << std::endl;
    TorrentTracker tracker(torrent_file.announce);

    bool should_request_more_peers = false;
    int tracker_attempts_count = 0;
    static constexpr int max_tracker_attempts_count = 5;

    do {
        try {
            tracker.UpdatePeers(torrent_file, peer_id, 12345);
            ++tracker_attempts_count;

            const auto& peers = tracker.GetPeers();
            if (peers.empty()) {
                std::cerr << "No peers found from tracker" << std::endl;
                if (tracker_attempts_count < max_tracker_attempts_count) {
                    std::this_thread::sleep_for(5s);
                    continue;
                } else {
                    throw std::runtime_error("Failed to get peers after " +
                                           std::to_string(max_tracker_attempts_count) + " attempts");
                }
            }

            std::cout << "Found " << peers.size() << " peers" << std::endl;
            should_request_more_peers = RunDownloadMultithread(pieces, torrent_file, tracker);

        } catch (const std::exception& e) {
            std::cerr << "Tracker error: " << e.what() << std::endl;
            if (tracker_attempts_count < max_tracker_attempts_count) {
                std::this_thread::sleep_for(5s);
                should_request_more_peers = true;
            } else {
                throw;
            }
        }
    } while (should_request_more_peers && tracker_attempts_count < max_tracker_attempts_count && !is_terminated);
}

void TorrentClient::DownloadTorrent(const std::filesystem::path& torrent_file_path,
                                   const std::filesystem::path& output_directory) {
    is_terminated = false;

    TorrentFile torrentFile = LoadTorrentFile(torrent_file_path);

    std::cout << "Downloading " << torrentFile.piece_hashes.size() << " pieces" << std::endl;
    std::cout << "File: " << torrentFile.name << " (" << torrentFile.length << " bytes)" << std::endl;
    std::cout << "Peer ID: " << peer_id << std::endl;

    PieceStorage pieces(torrentFile, output_directory, torrentFile.piece_hashes.size());

    auto start_time = std::chrono::steady_clock::now();
    DownloadFromTracker(torrentFile, pieces);
    auto end_time = std::chrono::steady_clock::now();

    pieces.CloseOutputFile();

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    std::cout << "Download completed in " << duration.count() << " seconds" << std::endl;
    std::cout << "Saved " << pieces.PiecesSavedToDiscCount() << " pieces to disk" << std::endl;
}
