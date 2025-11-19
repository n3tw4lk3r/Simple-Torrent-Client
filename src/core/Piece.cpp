#include "core/Piece.hpp"
#include "utils/byte_tools.hpp"

Piece::Piece(size_t index, size_t length, const std::string& hash)
    : index(index)
    , length(length)
    , hash(hash)
    {
        size_t bytesUsed = 0, offset = 0;
        for (; bytesUsed < length - kBlockSize; bytesUsed += kBlockSize) {
            blocks.push_back({ index, offset, kBlockSize, Block::Status(0), "" });
            offset += kBlockSize;
        }
        blocks.push_back({ index, offset, length - bytesUsed, Block::Status(0), "" });
    }

bool Piece::HashMatches() const {
    return GetDataHash() == hash;
}

Block* Piece::GetFirstMissingBlock() {
    for (Block& block : blocks) {
        if (block.status == Block::Status::kMissing) {
            block.status = Block::Status::kPending;
            return &block;
        }
    }
    return nullptr;
}

size_t Piece::GetIndex() const {
    return index;
}

void Piece::SaveBlock(size_t blockOffset, std::string data) {
    for (Block& block : blocks) {
        if (block.offset == blockOffset) {
            block.data = data;
            block.status = Block::Status::kRetreived;
            return;
        }
    }
}

bool Piece::AllBlocksRetrieved() const {
    for (const Block& block : blocks) {
        if (block.status != Block::Status::kRetreived) {
            return false;
        }
    }
    return true;
}

std::string Piece::GetData() const {
    std::string data;
    for (const Block& block : blocks) {
        data += block.data;
    }
    return data;
}

std::string Piece::GetDataHash() const {
    return utils::CalculateSHA1(GetData());
}

const std::string& Piece::GetHash() const {
    return hash;
}

void Piece::Reset() {
    for (Block& block : blocks) {
        block.data.clear();
        block.status = Block::Status::kMissing;
    }
}
