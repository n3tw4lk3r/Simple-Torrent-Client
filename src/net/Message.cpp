#include "net/Message.hpp"
#include "utils/byte_tools.hpp"

Message Message::Parse(const std::string& message_string) {
    size_t length = utils::BytesToInt(message_string.substr(0, 4));
    if (length == 0) {
        return { MessageId(10), 0, "" };
    }

    uint8_t id = uint8_t((unsigned char) message_string[4]);
    std::string payload;
    if (id > 3) {
        payload = message_string.substr(5);
    }

    return { MessageId(id), length, payload };
}

Message Message::Init(MessageId id, const std::string& payload) {
    return { id, payload.size() + 1, payload };
}

std::string Message::ToString() const {
    std::string message_id;
    unsigned char ch = static_cast<uint8_t>(id) & 0xFF;
    message_id += ch;
    return utils::IntToBytes(messageLength) + message_id + payload;
}
