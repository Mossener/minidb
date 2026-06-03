#include "storage/table_heap.h"

namespace minidb {

TableHeap::TableHeap(BufferPoolManager *bpm)
    : bpm_(bpm), first_page_id_(INVALID_PAGE_ID) {}

page_id_t TableHeap::AllocatePage() {
    page_id_t page_id;
    Page *page = bpm_->NewPage(&page_id);
    if (!page) return INVALID_PAGE_ID;
    PageHeader header;
    header.num_tuples = 0;
    header.free_space = PAGE_SIZE - PAGE_HEADER_SIZE;
    header.next_page_id = INVALID_PAGE_ID;
    memcpy(page->GetData(), &header, PAGE_HEADER_SIZE);
    bpm_->UnpinPage(page_id, true);
    return page_id;
}

bool TableHeap::InsertTuple(const Tuple &tuple, rid_t *rid, Transaction *txn) {
    int raw_size = static_cast<int>(tuple.GetLength());
    int storage_size = sizeof(int32_t) + static_cast<int>(sizeof(TupleMeta)) + raw_size;
    int page_need = storage_size;

    if (page_need > PAGE_SIZE - PAGE_HEADER_SIZE) return false;

    page_id_t page_id = first_page_id_;
    if (page_id == INVALID_PAGE_ID) {
        page_id = AllocatePage();
        if (page_id == INVALID_PAGE_ID) return false;
        first_page_id_ = page_id;
    }

    while (true) {
        Page *page = bpm_->FetchPage(page_id);
        PageHeader header;
        memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);

        if (header.free_space >= page_need) {
            int offset = PAGE_SIZE - header.free_space;

            int32_t raw_size_field = raw_size;
            memcpy(page->GetData() + offset, &raw_size_field, sizeof(int32_t));

            TupleMeta meta;
            meta.txn_id = txn->GetTxnId();
            meta.begin_ts = TS_UNCOMMITTED;
            meta.end_ts = TS_MAX;
            WriteTupleMeta(page->GetData(), offset, meta);

            tuple.SerializeTo(page->GetData() + offset + TUPLE_META_HEADER_SIZE);

            header.free_space -= page_need;
            header.num_tuples++;
            memcpy(page->GetData(), &header, PAGE_HEADER_SIZE);

            *rid = (static_cast<rid_t>(page_id) << 32) | (offset & 0xFFFFFFFF);
            txn->AddWriteRecord(*rid, TS_MAX, true);
            bpm_->UnpinPage(page_id, true);
            return true;
        }

        page_id_t next = header.next_page_id;
        if (next == INVALID_PAGE_ID) {
            next = AllocatePage();
            if (next == INVALID_PAGE_ID) {
                bpm_->UnpinPage(page_id, false);
                return false;
            }
            header.next_page_id = next;
            memcpy(page->GetData(), &header, PAGE_HEADER_SIZE);
        }
        bpm_->UnpinPage(page_id, false);
        page_id = next;
    }
}

bool TableHeap::DeleteTuple(rid_t rid, Transaction *txn) {
    page_id_t page_id = static_cast<page_id_t>(rid >> 32);
    int offset = static_cast<int>(rid & 0xFFFFFFFF);
    Page *page = bpm_->FetchPage(page_id);
    if (!page) return false;

    TupleMeta meta = ReadTupleMeta(page->GetData(), offset);
    txn->AddWriteRecord(rid, meta.end_ts, false); // save old end_ts before overwriting
    meta.end_ts = TS_UNCOMMITTED; // marked for deletion, not yet committed
    WriteTupleMeta(page->GetData(), offset, meta);
    bpm_->UnpinPage(page_id, true);
    return true;
}

Tuple TableHeap::GetTuple(const Schema &schema, rid_t rid, Transaction *txn,
                          TransactionManager *txn_mgr) {
    page_id_t page_id = static_cast<page_id_t>(rid >> 32);
    int offset = static_cast<int>(rid & 0xFFFFFFFF);
    Page *page = bpm_->FetchPage(page_id);
    if (!page) return Tuple();

    TupleMeta meta = ReadTupleMeta(page->GetData(), offset);
    int raw_size = GetRawDataSize(page->GetData(), offset);

    if (!txn_mgr->IsVisible(meta, txn->GetReadTs(), txn->GetTxnId())) {
        bpm_->UnpinPage(page_id, false);
        return Tuple(); // invisible -> return null tuple
    }

    std::vector<char> data(raw_size);
    memcpy(data.data(), page->GetData() + offset + TUPLE_META_HEADER_SIZE, raw_size);
    bpm_->UnpinPage(page_id, false);
    return Tuple(std::move(data), schema);
}

rid_t TableHeap::GetFirstRID(const Schema &schema) {
    if (first_page_id_ == INVALID_PAGE_ID) return -1;
    page_id_t pid = first_page_id_;
    Page *page = bpm_->FetchPage(pid);
    if (!page) return -1;
    PageHeader header;
    memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);
    if (header.num_tuples <= 0) {
        bpm_->UnpinPage(pid, false);
        return -1;
    }
    rid_t rid = (static_cast<rid_t>(pid) << 32) | (PAGE_HEADER_SIZE & 0xFFFFFFFF);
    bpm_->UnpinPage(pid, false);
    return rid;
}

rid_t TableHeap::GetNextRID(rid_t current_rid, const Schema &schema) {
    page_id_t page_id = static_cast<page_id_t>(current_rid >> 32);
    int offset = static_cast<int>(current_rid & 0xFFFFFFFF);
    Page *page = bpm_->FetchPage(page_id);
    if (!page) return -1;

    int storage_size = GetTupleStorageSize(page->GetData(), offset);
    int next_offset = offset + storage_size;

    PageHeader header;
    memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);

    if (next_offset < PAGE_SIZE - header.free_space) {
        bpm_->UnpinPage(page_id, false);
        return (static_cast<rid_t>(page_id) << 32) | (next_offset & 0xFFFFFFFF);
    }

    if (header.next_page_id == INVALID_PAGE_ID) {
        bpm_->UnpinPage(page_id, false);
        return -1;
    }
    page_id = header.next_page_id;
    page = bpm_->FetchPage(page_id);
    if (!page) return -1;
    memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);
    if (header.num_tuples <= 0) {
        bpm_->UnpinPage(page_id, false);
        return -1;
    }
    rid_t rid = (static_cast<rid_t>(page_id) << 32) | (PAGE_HEADER_SIZE & 0xFFFFFFFF);
    bpm_->UnpinPage(page_id, false);
    return rid;
}

} // namespace minidb
