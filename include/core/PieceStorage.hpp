#pragma once

#include "core/TorrentFile.hpp"
#include "core/Piece.hpp"
#include <queue>
#include <mutex>
#include <fstream>
#include <filesystem>

class PieceStorage {
public:
    PieceStorage(const TorrentFile& torrent_file, const std::filesystem::path& output_directory, int number_of_pieces_to_download);

    PiecePtr GetNextPieceToDownload();
    void PieceProcessed(const PiecePtr& piece);
    void Enqueue(const PiecePtr& piece);
    bool QueueIsEmpty() const;
    size_t PiecesSavedToDiscCount() const;
    size_t TotalPiecesCount() const;
    void CloseOutputFile();
    const std::vector<size_t>& GetPiecesSavedToDiscIndices() const;
    size_t PiecesInProgressCount() const;
    size_t getToDownloadCnt() const;

private:
    mutable std::mutex queue_mutex;
    mutable std::mutex file_mutex;
    size_t total_piece_count = 0;
    size_t number_of_pieces_to_download;
    size_t default_piece_length;
    std::queue<PiecePtr> remaining_pieces_queue;
    std::ofstream file;
    std::vector<size_t> indices_of_pieces_saved_to_disk;

    void SavePieceToDisk(const PiecePtr& piece);
};
