#pragma once

#include <cstring>
#include "common/config.h"

namespace minidb {

using ts_t = int64_t;

static constexpr ts_t TS_MAX = INT64_MAX;
static constexpr ts_t TS_UNCOMMITTED = 0;

struct TupleMeta {
    txn_id_t txn_id;
    ts_t begin_ts;
    ts_t end_ts;
};

// Storage layout per tuple:
// [raw_data_size : int32_t]         | 4 bytes
// [txn_id        : int64_t]         | 8 bytes
// [begin_ts      : int64_t]         | 8 bytes
// [end_ts        : int64_t]         | 8 bytes
// [raw_data      : raw_data_size]   | variable

static constexpr int TUPLE_META_HEADER_SIZE = sizeof(int32_t) + sizeof(TupleMeta);

inline TupleMeta ReadTupleMeta(const char *page_data, int offset) {
    TupleMeta meta;
    const char *p = page_data + offset + sizeof(int32_t);
    memcpy(&meta.txn_id, p, sizeof(int64_t));
    memcpy(&meta.begin_ts, p + sizeof(int64_t), sizeof(int64_t));
    memcpy(&meta.end_ts, p + 2 * sizeof(int64_t), sizeof(int64_t));
    return meta;
}

inline void WriteTupleMeta(char *page_data, int offset, const TupleMeta &meta) {
    char *p = page_data + offset + sizeof(int32_t);
    memcpy(p, &meta.txn_id, sizeof(int64_t));
    memcpy(p + sizeof(int64_t), &meta.begin_ts, sizeof(int64_t));
    memcpy(p + 2 * sizeof(int64_t), &meta.end_ts, sizeof(int64_t));
}

inline int GetRawDataSize(const char *page_data, int offset) {
    int32_t s;
    memcpy(&s, page_data + offset, sizeof(int32_t));
    return s;
}

inline int GetTupleStorageSize(const char *page_data, int offset) {
    return TUPLE_META_HEADER_SIZE + GetRawDataSize(page_data, offset);
}

inline int GetTupleDataOffset() {
    return TUPLE_META_HEADER_SIZE;
}

} // namespace minidb
