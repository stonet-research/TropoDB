// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
/**
 * This logic is heavily based on the TwoLevelIterator from LevelDB
 */
#pragma once
#ifdef ZNS_PLUGIN_ENABLED
#ifndef ZNS_SSTABLE_LN_ITERATOR_H
#define ZNS_SSTABLE_LN_ITERATOR_H

#include "db/dbformat.h"
#include "db/zns_impl/table/iterators/iterator_wrapper.h"
#include "db/zns_impl/zns_sstable_manager.h"
#include "db/zns_impl/zns_zonemetadata.h"
#include "rocksdb/iterator.h"
#include "rocksdb/slice.h"
#include "rocksdb/status.h"

namespace ROCKSDB_NAMESPACE {
/**
 * Iterates over individual SSTables in a Vector of ZNSMetadata.
 */
class LNZoneIterator : public Iterator {
 public:
  LNZoneIterator(const InternalKeyComparator& icmp,
                 const std::vector<SSZoneMetaData*>* slist);
  ~LNZoneIterator();
  bool Valid() const override { return index_ < slist_->size(); }
  Slice key() const override {
    assert(Valid());
    return (*slist_)[index_]->largest.Encode();
  }
  Slice value() const override {
    assert(Valid());
    EncodeFixed64(value_buf_, (*slist_)[index_]->number);
    EncodeFixed64(value_buf_ + 8, (*slist_)[index_]->lba_count);
    return Slice(value_buf_, sizeof(value_buf_));
  }
  Status status() const override { return Status::OK(); }
  void Seek(const Slice& target) override;
  void SeekForPrev(const Slice& target) override;
  void SeekToFirst() override;
  void SeekToLast() override;
  void Next() override;
  void Prev() override;

 private:
  const InternalKeyComparator icmp_;
  const std::vector<SSZoneMetaData*>* const slist_;
  size_t index_;

  mutable char value_buf_[16];
};

typedef Iterator* (*NewZoneIteratorFunction)(void*, const Slice&);
class LNIterator : public Iterator {
 public:
  LNIterator(Iterator* ln_iterator, NewZoneIteratorFunction zone_function,
             void* arg);
  ~LNIterator() override;
  bool Valid() const override { return data_iter_.Valid(); }
  Slice key() const override {
    assert(Valid());
    return data_iter_.key();
  }
  Slice value() const override {
    assert(Valid());
    return data_iter_.value();
  }
  Status status() const override { return Status::OK(); }
  void Seek(const Slice& target) override;
  void SeekForPrev(const Slice& target) override;
  void SeekToFirst() override;
  void SeekToLast() override;
  void Next() override;
  void Prev() override;

 private:
  void SkipEmptyDataLbasForward();
  void SkipEmptyDataLbasBackward();
  void SetDataIterator(Iterator* data_iter);
  void InitDataZone();

  NewZoneIteratorFunction zone_function_;
  void* arg_;
  IteratorWrapper index_iter_;
  IteratorWrapper data_iter_;
  std::string data_zone_handle_;
};
}  // namespace ROCKSDB_NAMESPACE

#endif
#endif