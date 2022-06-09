#include "db/zns_impl/table/iterators/sstable_ln_iterator.h"

#include "db/dbformat.h"
#include "db/zns_impl/table/zns_sstable.h"
#include "db/zns_impl/table/zns_sstable_manager.h"
#include "db/zns_impl/table/zns_zonemetadata.h"
#include "rocksdb/slice.h"

namespace ROCKSDB_NAMESPACE {
LNZoneIterator::LNZoneIterator(const Comparator* cmp,
                               const std::vector<SSZoneMetaData*>* slist,
                               const uint8_t level)
    : cmp_(cmp), level_(level), slist_(slist), index_(slist->size()) {}

LNZoneIterator::~LNZoneIterator() = default;

Slice LNZoneIterator::value() const {
  assert(Valid());
  // printf("Encoding %u %lu %lu %u ", (*slist_)[index_]->LN.lba_regions,
  //        (*slist_)[index_]->number, (*slist_)[index_]->lba_count, level_);
  EncodeFixed8(value_buf_, (*slist_)[index_]->LN.lba_regions);
  for (size_t i = 0; i < (*slist_)[index_]->LN.lba_regions; i++) {
    EncodeFixed64(value_buf_ + 1 + i * 16, (*slist_)[index_]->LN.lbas[i]);
    EncodeFixed64(value_buf_ + 9 + i * 16,
                  (*slist_)[index_]->LN.lba_region_sizes[i]);
    // printf(" - %lu %lu ", (*slist_)[index_]->LN.lbas[i],
    //        (*slist_)[index_]->LN.lba_region_sizes[i]);
  }
  // printf("\n");
  EncodeFixed64(value_buf_ + 1 + 16 * (*slist_)[index_]->LN.lba_regions,
                (*slist_)[index_]->lba_count);
  EncodeFixed8(value_buf_ + 9 + 16 * (*slist_)[index_]->LN.lba_regions, level_);
  EncodeFixed64(value_buf_ + 10 + 16 * (*slist_)[index_]->LN.lba_regions,
                (*slist_)[index_]->number);
  return Slice(value_buf_, sizeof(value_buf_));
}

void LNZoneIterator::Seek(const Slice& target) {
  index_ = ZNSSSTableManager::FindSSTableIndex(cmp_, *slist_, target);
}

void LNZoneIterator::SeekForPrev(const Slice& target) {
  Seek(target);
  Prev();
}

void LNZoneIterator::SeekToFirst() { index_ = 0; }

void LNZoneIterator::SeekToLast() {
  index_ = slist_->empty() ? 0 : slist_->size() - 1;
}

void LNZoneIterator::Next() {
  assert(Valid());
  index_++;
}

void LNZoneIterator::Prev() {
  assert(Valid());
  index_ = index_ == 0 ? slist_->size() : index_ - 1;
}

LNIterator::LNIterator(Iterator* ln_iterator,
                       NewZoneIteratorFunction zone_function, void* arg,
                       const Comparator* cmp)
    : zone_function_(zone_function),
      arg_(arg),
      index_iter_(ln_iterator),
      data_iter_(nullptr),
      cmp_(cmp) {}

LNIterator::~LNIterator() = default;

void LNIterator::Seek(const Slice& target) {
  index_iter_.Seek(target);
  InitDataZone();
  if (data_iter_.iter() != nullptr) data_iter_.Seek(target);
  SkipEmptyDataLbasForward();
}

void LNIterator::SeekForPrev(const Slice& target) {
  Seek(target);
  Prev();
}

void LNIterator::SeekToFirst() {
  index_iter_.SeekToFirst();
  InitDataZone();
  if (data_iter_.iter() != nullptr) data_iter_.SeekToFirst();
  SkipEmptyDataLbasForward();
}

void LNIterator::SeekToLast() {
  index_iter_.SeekToLast();
  InitDataZone();
  if (data_iter_.iter() != nullptr) data_iter_.SeekToLast();
  SkipEmptyDataLbasForward();
}

void LNIterator::Next() {
  assert(Valid());
  data_iter_.Next();
  SkipEmptyDataLbasForward();
}

void LNIterator::Prev() {
  assert(Valid());
  data_iter_.Next();
  SkipEmptyDataLbasBackward();
}

void LNIterator::SkipEmptyDataLbasForward() {
  while (data_iter_.iter() == nullptr || !data_iter_.Valid()) {
    if (!index_iter_.Valid()) {
      SetDataIterator(nullptr);
      return;
    }
    index_iter_.Next();
    InitDataZone();
    if (data_iter_.iter() != nullptr) data_iter_.SeekToFirst();
  }
}

void LNIterator::SkipEmptyDataLbasBackward() {
  while (data_iter_.iter() == nullptr || !data_iter_.Valid()) {
    if (!index_iter_.Valid()) {
      SetDataIterator(nullptr);
      return;
    }
    index_iter_.Prev();
    InitDataZone();
    if (data_iter_.iter() != nullptr) data_iter_.SeekToFirst();
  }
}

void LNIterator::SetDataIterator(Iterator* data_iter) {
  data_iter_.Set(data_iter);
}

void LNIterator::InitDataZone() {
  if (!index_iter_.Valid()) {
    SetDataIterator(nullptr);
    return;
  }
  Slice handle = index_iter_.value();
  if (data_iter_.iter() != nullptr && handle.compare(data_zone_handle_) == 0) {
    return;
  }
  Iterator* iter = (*zone_function_)(arg_, handle, cmp_);
  data_zone_handle_.assign(handle.data(), handle.size());
  SetDataIterator(iter);
}
}  // namespace ROCKSDB_NAMESPACE
