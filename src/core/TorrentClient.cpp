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
                                           const TorrentTracker& tracker,
                                           bool is_final_attempt) {
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
            static constexpr int max_number_of_attempts = 5;
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
    int no_progress_count = 0;

    std::cout << "=== DOWNLOAD ";
    if (is_final_attempt)
        std::cout << "FINAL ATTEMPT";
    else
        std::cout << "STARTED";
    std::cout << " ===" << std::endl;
    std::cout << "Target pieces: " << target_pieces << std::endl;
    std::cout << "Initial saved pieces: " << pieces.PiecesSavedToDiscCount() << std::endl;
    pieces.PrintMissingPieces();

    auto stall_timeout = 30s;
    if (is_final_attempt)
        stall_timeout = 60s;
    auto check_interval = 1000ms;
    if (is_final_attempt)
        check_interval = 500ms;  

    while (!is_terminated && pieces.PiecesSavedToDiscCount() < target_pieces) {
        std::this_thread::sleep_for(check_interval);

        auto now = std::chrono::steady_clock::now();
        size_t current_saved_count = pieces.PiecesSavedToDiscCount();

        if (current_saved_count == last_saved_count) {
            no_progress_count++;
            if (no_progress_count % 10 == 0) {
                std::cout << "No progress for " << no_progress_count << " seconds" << std::endl;

                if (no_progress_count % 30 == 0) {
                    std::cout << "=== STALL DIAGNOSTICS ===" << std::endl;
                    pieces.PrintMissingPieces();

                    size_t active_peers = 0;
                    for (const auto& peer : peer_connections) {
                        if (!peer->Failed()) {
                            active_peers++;
                        }
                    }
                    std::cout << "Active peer connections: " << active_peers << "/" << peer_connections.size() << std::endl;
                }
            }

            if (now - last_progress_time > stall_timeout) {
                std::cout << "Download stalled, requesting new peers..." << std::endl;
                pieces.PrintMissingPieces();
                break;
            }
        } else {
            no_progress_count = 0;
            last_saved_count = current_saved_count;
            last_progress_time = now;

            if (current_saved_count % 5 == 0 || current_saved_count == target_pieces) {
                std::cout << "Progress: " << current_saved_count << "/" << target_pieces
                          << " pieces (" << (current_saved_count * 100 / target_pieces) << "%)"
                          << std::endl;

                if (current_saved_count % 50 == 0) {
                    pieces.PrintMissingPieces();
                }
            }
        }

        if (pieces.QueueIsEmpty() && current_saved_count < target_pieces) {
            std::cout << "WARNING: Queue is empty but download not complete! "
                      << "Saved: " << current_saved_count << "/" << target_pieces << std::endl;
            pieces.PrintMissingPieces();

            if (is_final_attempt) {
                std::cout << "FINAL ATTEMPT: Forcing missing pieces back to queue..." << std::endl;
                pieces.ForceRequeueMissingPieces();
                std::this_thread::sleep_for(2s);
                continue;
            }

            std::this_thread::sleep_for(5s);
            if (pieces.QueueIsEmpty() && pieces.PiecesSavedToDiscCount() == current_saved_count) {
                std::cout << "Confirmed: No progress with empty queue. Breaking..." << std::endl;
                break;
            }
        }
    }

    std::cout << "=== DOWNLOAD FINISHED ===" << std::endl;
    std::cout << "Final piece count: " << pieces.PiecesSavedToDiscCount() << "/" << target_pieces << std::endl;

    is_terminated = true;

    std::cout << "Terminating peer connections..." << std::endl;
    for (auto& peer_connect_ptr : peer_connections) {
        peer_connect_ptr->Terminate();
    }

    std::cout << "Waiting for peer threads to finish..." << std::endl;
    for (auto& thread : peer_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::cout << "=== FINAL DIAGNOSTICS ===" << std::endl;
    pieces.PrintMissingPieces();

    bool download_complete = pieces.PiecesSavedToDiscCount() >= target_pieces;
    if (download_complete) {
        std::cout << "DOWNLOAD COMPLETE SUCCESSFULLY!" << std::endl;
    } else {
        std::cout << "DOWNLOAD INCOMPLETE! Missing "
                  << (target_pieces - pieces.PiecesSavedToDiscCount())
                  << " pieces." << std::endl;
    }

    return !download_complete;
}

void TorrentClient::DownloadFromTracker(const TorrentFile& torrent_file, PieceStorage& pieces) {
    std::cout << "Connecting to tracker " << torrent_file.announce << std::endl;
    TorrentTracker tracker(torrent_file.announce);

    int tracker_attempts = 0;
    constexpr int max_tracker_attempts = 10;

    while (!is_terminated && !pieces.IsDownloadComplete()) {
        try {
            tracker_attempts++;
            std::cout << "Tracker attempt " << tracker_attempts << "..." << std::endl;

            tracker.UpdatePeers(torrent_file, peer_id, 12345);
            const auto& peers = tracker.GetPeers();

            if (peers.empty()) {
                std::cout << "No peers found, retrying in 10 seconds..." << std::endl;
                std::this_thread::sleep_for(10s);
                continue;
            }

            std::cout << "Found " << peers.size() << " peers" << std::endl;
            RunDownloadMultithread(pieces, torrent_file, tracker, false);

            if (!pieces.IsDownloadComplete()) {
                std::cout << "Download incomplete, requesting new peers in 15 seconds..." << std::endl;
                std::this_thread::sleep_for(15s);
            }

        } catch (const std::exception& e) {
            std::cerr << "Tracker error: " << e.what() << std::endl;
            if (tracker_attempts < max_tracker_attempts) {
                std::cout << "Retrying in 10 seconds..." << std::endl;
                std::this_thread::sleep_for(10s);
            } else {
                std::cerr << "Too many tracker failures, but continuing..." << std::endl;
                std::this_thread::sleep_for(30s);
                tracker_attempts = 0;
            }
        }
    }
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

    size_t saved_count = pieces.PiecesSavedToDiscCount();
    size_t total_count = torrentFile.piece_hashes.size();

    std::cout << "=== TORRENT DOWNLOAD FINISHED ===" << std::endl;
    std::cout << "Download time: " << duration.count() << " seconds" << std::endl;
    std::cout << "Saved " << saved_count << "/" << total_count << " pieces to disk" << std::endl;
    std::cout << "Completion: " << (saved_count * 100 / total_count) << "%" << std::endl;

    if (saved_count == total_count) {
        std::cout << "Download completed successfully!" << std::endl;
    } else {
        std::cout << "Download interrupted! Missing " << (total_count - saved_count) << " pieces." << std::endl;
        pieces.PrintMissingPieces();
    }
}
