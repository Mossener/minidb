#include "storage/tuple.h"
#include <cstring>

namespace minidb {

Tuple::Tuple(std::vector<char> data, const Schema &schema)
    : data_(std::move(data)) {}

void Tuple::SerializeTo(char *dest) const {
    memcpy(dest, data_.data(), data_.size());
}

Tuple Tuple::DeserializeFrom(const char *src, const Schema &schema) {
    std::vector<char> data(src, src + schema.GetSerializedSize());
    return Tuple(std::move(data), schema);
}

Tuple Tuple::MakeNumericTuple(const Schema &schema, int val) {
    std::vector<char> data(schema.GetSerializedSize(), 0);
    memcpy(data.data(), &val, sizeof(val));
    return Tuple(std::move(data), schema);
}

void Tuple::SetValueAt(size_t offset, const char *val, size_t len) {
    if (offset + len <= data_.size()) {
        memcpy(data_.data() + offset, val, len);
    }
}

} // namespace minidb
