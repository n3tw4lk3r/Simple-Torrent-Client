#pragma once

#include "core/Piece.hpp"
#include "core/TorrentFile.hpp"
#include <filesystem>
#include <fstream>
#include <queue>
#include <mutex>
#include <vector>

class PieceStorage {
public:
    PieceStorage(const TorrentFile& torrent_file,
                 const std::filesystem::path& output_directory);

    PiecePtr GetNextPieceToDownload();
    void PieceProcessed(const PiecePtr& piece);
    void Enqueue(const PiecePtr& piece);
    bool QueueIsEmpty() const;
    bool IsPieceAlreadySaved(size_t piece_index) const;
    size_t TotalPiecesCount() const;
    size_t PiecesSavedToDiscCount() const;

    void CloseOutputFile();
    void PrintMissingPieces() const;
    bool IsDownloadComplete() const;
    bool HasActiveWork() const;
    std::vector<size_t> GetMissingPieces() const;
    void ForceRequeueMissingPieces();

    void PrintDownloadStatus() const;
    void PrintDetailedStatus() const;
    size_t GetMissingPiecesCount() const;
private:
    void SavePieceToDisk(const PiecePtr& piece);
    void InitializeOutputFile();

    std::queue<PiecePtr> remaining_pieces_queue;
    mutable std::mutex queue_mutex;
    std::ofstream file;
    mutable std::mutex file_mutex;
    std::vector<size_t> indices_of_pieces_saved_to_disk;

    std::filesystem::path output_directory;
    size_t default_piece_length;
    size_t total_piece_count;
    TorrentFile torrent_file;
};
