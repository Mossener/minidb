#pragma once

#include <cstdint>
#include <chrono>

namespace minidb {

static constexpr int PAGE_SIZE = 4096;
static constexpr int BUFFER_POOL_SIZE = 128;
static constexpr int MAX_DATABASES = 64;
static constexpr int MAX_TABLES_PER_DB = 256;
static constexpr int BPLUS_TREE_DEGREE = 64;
static constexpr int MAX_COLUMNS = 256;
static constexpr int MAX_TUPLE_SIZE = PAGE_SIZE - 128;

using page_id_t = int32_t;
using frame_id_t = int32_t;
using table_id_t = int32_t;
using column_id_t = int16_t;
using rid_t = int64_t;
using txn_id_t = int64_t;

static constexpr page_id_t INVALID_PAGE_ID = -1;
static constexpr frame_id_t INVALID_FRAME_ID = -1;
static constexpr txn_id_t INVALID_TXN_ID = -1;

} // namespace minidb
