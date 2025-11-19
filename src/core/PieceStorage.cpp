#include "core/PieceStorage.hpp"
#include "core/Piece.hpp"
#include <iostream>
#include <cmath>

PieceStorage::PieceStorage(const TorrentFile& torrent_file, const std::filesystem::path& output_directory,
                          int piecesToDownloadCnt)
    : default_piece_length(torrent_file.piece_length)
    , number_of_pieces_to_download(piecesToDownloadCnt) {

    total_piece_count = torrent_file.piece_hashes.size();
    for (size_t i = 0; i < total_piece_count; ++i) {
        size_t pieceLength;
        if (i == total_piece_count - 1)
            pieceLength = torrent_file.length % torrent_file.piece_length;
        else
            pieceLength = torrent_file.piece_length;
        if (pieceLength == 0)
            pieceLength = torrent_file.piece_length;

        auto piece = std::make_shared<Piece>(i, pieceLength, torrent_file.piece_hashes[i]);
        remaining_pieces_queue.push(piece);
    }

    std::string filename = (output_directory / torrent_file.name).generic_string();
    file.open(filename, std::ios::out | std::ios::binary | std::ios::trunc);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open output file: " + filename);
    }

    file.seekp(torrent_file.length - 1);
    file.write("\0", 1);
    file.flush();
}

PiecePtr PieceStorage::GetNextPieceToDownload() {
    std::lock_guard<std::mutex> lock(queue_mutex);

    if (remaining_pieces_queue.empty()) {
        return nullptr;
    }

    PiecePtr piece = remaining_pieces_queue.front();
    remaining_pieces_queue.pop();
    return piece;
}

void PieceStorage::PieceProcessed(const PiecePtr& piece) {
    if (!piece->HashMatches()) {
        std::cout << "Hash mismatch for piece " << piece->GetIndex() << ", requeuing..." << std::endl;
        piece->Reset();
        Enqueue(piece);
        return;
    }

    SavePieceToDisk(piece);
}

void PieceStorage::Enqueue(const PiecePtr& piece) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    remaining_pieces_queue.push(piece);
}

bool PieceStorage::QueueIsEmpty() const {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return remaining_pieces_queue.empty();
}

size_t PieceStorage::TotalPiecesCount() const {
    return total_piece_count;
}

size_t PieceStorage::PiecesSavedToDiscCount() const {
    std::lock_guard<std::mutex> lock(file_mutex);
    return indices_of_pieces_saved_to_disk.size();
}

void PieceStorage::CloseOutputFile() {
    std::lock_guard<std::mutex> lock(file_mutex);
    if (file.is_open()) {
        file.close();
    }
}

const std::vector<size_t>& PieceStorage::GetPiecesSavedToDiscIndices() const {
    std::lock_guard<std::mutex> lock(file_mutex);
    return indices_of_pieces_saved_to_disk;
}

size_t PieceStorage::PiecesInProgressCount() const {
    std::lock_guard<std::mutex> queueLock(queue_mutex);
    std::lock_guard<std::mutex> fileLock(file_mutex);

    return total_piece_count - remaining_pieces_queue.size() - indices_of_pieces_saved_to_disk.size();
}

void PieceStorage::SavePieceToDisk(const PiecePtr& piece) {
    std::lock_guard<std::mutex> lock(file_mutex);

    try {
        size_t file_offset = piece->GetIndex() * default_piece_length;
        const std::string& piece_data = piece->GetData();

        file.seekp(file_offset);
        file.write(piece_data.data(), piece_data.size());
        file.flush();

        indices_of_pieces_saved_to_disk.push_back(piece->GetIndex());

        std::cout << "Saved piece " << piece->GetIndex() << " ("
                  << piece_data.size() << " bytes)" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Failed to save piece " << piece->GetIndex() << " to disk: "
                  << e.what() << std::endl;
        throw;
    }
}

size_t PieceStorage::getToDownloadCnt() const {
    return number_of_pieces_to_download;
}
