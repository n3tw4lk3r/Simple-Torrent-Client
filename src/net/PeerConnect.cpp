#include "net/PeerConnect.hpp"
#include "utils/byte_tools.hpp"
#include "net/Message.hpp"
#include <thread>
#include <algorithm>

using namespace std::chrono_literals;

PeerPiecesAvailability::PeerPiecesAvailability(std::string bitfield, size_t size) :
    bitfield(std::move(bitfield)),
    size(size) {}

bool PeerPiecesAvailability::IsPieceAvailable(size_t piece_index) const {
    if (piece_index >= size * 8)
        return false;
    return (bitfield[piece_index >> 3] >> (7 - (piece_index % 8))) & 1;
}

void PeerPiecesAvailability::SetPieceAvailability(size_t pieceIndex) {
    if (pieceIndex < size << 3) {
        bitfield[pieceIndex >> 3] |= (1 << (7 - (pieceIndex % 8)));
    }
}

size_t PeerPiecesAvailability::Size() const {
    return size;
}

PeerConnect::PeerConnect(const Peer& peer, const TorrentFile &torrent_file,
                         std::string self_peer_id, PieceStorage& piece_storage)
    : socket(peer.ip, peer.port, 15000ms, 30000ms)
    , torrent_file(torrent_file)
    , self_peer_id(std::move(self_peer_id))
    , piece_storage(piece_storage)
    , pieces_availability("", 0) {}

void PeerConnect::Run() {
    while (!is_terminated) {
        try {
            if (EstablishConnection()) {
                MainLoop();
            } else {
                has_failed = true;
                break;
            }
        } catch (const std::exception& e) {
            std::cerr << "Peer " << socket.GetIp() << ":" << socket.GetPort()
                      << " error: " << e.what() << std::endl;
            has_failed = true;

            if (!is_terminated) {
                std::this_thread::sleep_for(2s);
            }
        }

        if (!is_terminated && has_failed) {
            Terminate();
        }
    }
}

void PeerConnect::PerformHandshake() {
    std::string handshake_message;
    handshake_message += static_cast<char>(19); // pstrlen
    handshake_message += "BitTorrent protocol"; // pstr
    handshake_message += std::string(8, '\0'); // reserved
    handshake_message += torrent_file.info_hash; // info_hash
    handshake_message += self_peer_id; // peer_id

    socket.SendData(handshake_message);

    static constexpr int kResponseSize = 68;
    std::string response = socket.ReceiveData(kResponseSize);

    if (response.size() < kResponseSize) {
        throw std::runtime_error("Handshake response too short");
    }

    static constexpr int kPeerInfoHashSize = 28;
    std::string peer_info_hash = response.substr(kPeerInfoHashSize, 20);
    if (peer_info_hash != torrent_file.info_hash) {
        throw std::runtime_error("Peer sent mismatching info hash");
    }

    static constexpr int kPeerIdSize = 48;
    peer_id = response.substr(kPeerIdSize, 20);
}

bool PeerConnect::EstablishConnection() {
    try {
        socket.EstablishConnection();
        PerformHandshake();
        ReceiveBitfield();
        SendInterested();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to establish connection with " << socket.GetIp()
                  << ":" << socket.GetPort() << " - " << e.what() << std::endl;
        return false;
    }
}

void PeerConnect::ReceiveBitfield() {
    std::string message = socket.ReceiveData();

    if (message.empty()) {
        throw std::runtime_error("Empty message received");
    }

    int messageLength = utils::BytesToInt(message.substr(0, 4));
    if (messageLength == 0) {
        return; // Keep-alive
    }

    uint8_t messageId = static_cast<uint8_t>(message[4]);

    if (messageId == static_cast<uint8_t>(MessageId::kUnchoke)) {
        is_choked = false;
        return;
    }

    if (messageId == static_cast<uint8_t>(MessageId::kBitField)) {
        std::string bitfield = message.substr(5);
        size_t bitfield_size = (torrent_file.piece_hashes.size() + 7) >> 3; // ceil(pieceCount / 8)
        pieces_availability = PeerPiecesAvailability(bitfield, bitfield_size);
    }
}

void PeerConnect::SendInterested() {
    Message message = Message::Init(MessageId::kInterested, "");
    socket.SendData(message.ToString());
}

void PeerConnect::Terminate() {
    is_terminated = true;
    try {
        socket.CloseConnection();
    } catch (const std::exception& e) {
        // Ignore errors during cleanup
    }
}

void PeerConnect::MainLoop() {
    auto last_activity_time = std::chrono::steady_clock::now();
    constexpr auto inactivity_timeout = 120s;

    while (!is_terminated) {
        try {
            auto now = std::chrono::steady_clock::now();
            if (now - last_activity_time > inactivity_timeout) {
                throw std::runtime_error("Connection timeout due to inactivity");
            }

            if (!piece_is_in_progress || piece_is_in_progress->AllBlocksRetrieved()) {
                piece_is_in_progress = GetNextAvailablePiece();
                if (!piece_is_in_progress) {
                    if (piece_storage.QueueIsEmpty()) {
                        break;
                    }
                    std::this_thread::sleep_for(100ms);
                    continue;
                }
            }

            if (!is_choked && !block_is_pending) {
                Block* block = piece_is_in_progress->GetFirstMissingBlock();
                if (block) {
                    RequestPiece(block);
                    block_is_pending = true;
                    last_activity_time = now;
                }
            }

            std::string received_data = socket.ReceiveData();
            if (!received_data.empty()) {
                ProcessMessage(received_data);
                last_activity_time = std::chrono::steady_clock::now();
            }

        } catch (const std::exception& e) {
            if (!is_terminated) {
                if (piece_is_in_progress && !piece_is_in_progress->AllBlocksRetrieved()) {
                    piece_storage.Enqueue(piece_is_in_progress);
                }
                throw;
            }
        }
    }
}

PiecePtr PeerConnect::GetNextAvailablePiece() {
    static constexpr int max_number_of_attempts = 100;

    for (int attempt = 0; attempt < max_number_of_attempts; ++attempt) {
        PiecePtr piece = piece_storage.GetNextPieceToDownload();
        if (!piece) break;

        if (pieces_availability.IsPieceAvailable(piece->GetIndex())) {
            return piece;
        }

        piece_storage.Enqueue(piece);
    }

    return nullptr;
}

void PeerConnect::ProcessMessage(const std::string& message_data) {
    Message message = Message::Parse(message_data);

    switch (message.id) {
        case MessageId::kChoke:
            is_choked = true;
            block_is_pending = false;
            break;

        case MessageId::kUnchoke:
            is_choked = false;
            break;

        case MessageId::kHave: {
            if (message.payload.size() >= 4) {
                size_t pieceIndex = utils::BytesToInt(message.payload.substr(0, 4));
                pieces_availability.SetPieceAvailability(pieceIndex);
            }
            break;
        }

        case MessageId::kPiece: {
            if (message.payload.size() >= 8) {
                size_t piece_index = utils::BytesToInt(message.payload.substr(0, 4));
                size_t block_offset = utils::BytesToInt(message.payload.substr(4, 4));
                std::string block_data = message.payload.substr(8);

                if (piece_is_in_progress && piece_is_in_progress->GetIndex() == piece_index) {
                    piece_is_in_progress->SaveBlock(block_offset, block_data);
                    block_is_pending = false;

                    if (piece_is_in_progress->AllBlocksRetrieved()) {
                        if (piece_is_in_progress->HashMatches()) {
                            piece_storage.PieceProcessed(piece_is_in_progress);
                            piece_is_in_progress.reset();
                        } else {
                            piece_is_in_progress->Reset();
                            piece_storage.Enqueue(piece_is_in_progress);
                            piece_is_in_progress.reset();
                        }
                    }
                }
            }
            break;
        }

        case MessageId::kKeepAlive:
        default:
            break;
    }
}

void PeerConnect::RequestPiece(const Block* block) {
    if (!block)
        return;

    std::string payload;
    payload += utils::IntToBytes(static_cast<uint32_t>(block->piece));
    payload += utils::IntToBytes(static_cast<uint32_t>(block->offset));
    payload += utils::IntToBytes(static_cast<uint32_t>(block->length));

    Message message = Message::Init(MessageId::kRequest, payload);
    socket.SendData(message.ToString());
}

bool PeerConnect::Failed() const {
    return has_failed;
}
