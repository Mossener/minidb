#pragma once

#include <vector>
#include <cstring>
#include <cassert>
#include "common/config.h"
#include "storage/schema.h"

namespace minidb {

class Tuple {
public:
    Tuple() = default;
    Tuple(std::vector<char> data, const Schema &schema);

    const char *GetData() const { return data_.data(); }
    char *GetData() { return data_.data(); }
    size_t GetLength() const { return data_.size(); }
    bool IsNull() const { return data_.empty(); }

    void SerializeTo(char *dest) const;
    static Tuple DeserializeFrom(const char *src, const Schema &schema);

    static Tuple MakeNumericTuple(const Schema &schema, int val);
    void SetValueAt(size_t offset, const char *val, size_t len);

private:
    std::vector<char> data_;
};

} // namespace minidb
