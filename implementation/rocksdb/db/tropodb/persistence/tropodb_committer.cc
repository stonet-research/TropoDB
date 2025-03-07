#include "db/tropodb/persistence/tropodb_committer.h"

#include "db/tropodb/io/szd_port.h"
#include "db/tropodb/tropodb_config.h"
#include "db/tropodb/utils/tropodb_logger.h"
#include "db/write_batch_internal.h"
#include "rocksdb/slice.h"
#include "rocksdb/status.h"
#include "rocksdb/types.h"
#include "rocksdb/write_batch.h"
#include "util/coding.h"
#include "util/crc32c.h"

// TODO: This file requires a refactor, test and general cleanup.
// There are errors/possible corruptions in this code.

namespace ROCKSDB_NAMESPACE {
static void InitTypeCrc(
    std::array<uint32_t, kTropoRecordTypeLast + 1>& type_crc) {
  for (uint32_t i = 0; i <= type_crc.size(); i++) {
    char t = static_cast<char>(i);
    type_crc[i] = crc32c::Value(&t, 1);
  }
}

TropoCommitter::TropoCommitter(SZD::SZDLog* log, const SZD::DeviceInfo& info,
                               bool keep_buffer)
    : zone_cap_(info.zone_cap),
      lba_size_(info.lba_size),
      zasl_(info.zasl),
      number_of_readers_(log->GetNumberOfReaders()),
      log_(log),
      read_buffer_(nullptr),
      write_buffer_(0, info.lba_size),
      keep_buffer_(keep_buffer) {
  InitTypeCrc(type_crc_);
  read_buffer_ = new SZD::SZDBuffer*[number_of_readers_];
  for (uint8_t i = 0; i < number_of_readers_; i++) {
    read_buffer_[i] = new SZD::SZDBuffer(0, lba_size_);
  }
}

TropoCommitter::~TropoCommitter() {
  for (uint8_t i = 0; i < number_of_readers_; i++) {
    delete read_buffer_[i];
  }
  delete[] read_buffer_;
}

size_t TropoCommitter::SpaceNeeded(size_t data_size) const {
  size_t fragcount = data_size / lba_size_ + 1;
  size_t size_needed = fragcount * kTropoHeaderSize + data_size;
  size_needed = ((size_needed + lba_size_ - 1) / lba_size_) * lba_size_;
  return size_needed;
}

bool TropoCommitter::SpaceEnough(size_t size) const {
  return log_->SpaceLeft(SpaceNeeded(size));
}

bool TropoCommitter::SpaceEnough(const Slice& data) const {
  return SpaceEnough(data.size());
}

Status TropoCommitter::CommitToCharArray(const Slice& in, char** out) {
  assert(out != nullptr);
  Status s = Status::OK();
  const char* ptr = in.data();
  size_t walker = 0;
  size_t left = in.size();

  size_t size_needed = SpaceNeeded(left);
  *out = new char[size_needed];
  char* fragment = *out;

  bool begin = true;
  do {
    // determine next fragment part.
    size_t avail = lba_size_;
    avail = avail > kTropoHeaderSize ? avail - kTropoHeaderSize : 0;
    const size_t fragment_length = (left < avail) ? left : avail;

    TropoRecordType type;
    const bool end = (left == fragment_length);
    if (begin && end) {
      type = TropoRecordType::kFullType;
    } else if (begin) {
      type = TropoRecordType::kFirstType;
    } else if (end) {
      type = TropoRecordType::kLastType;
    } else {
      type = TropoRecordType::kMiddleType;
    }
    size_t frag_begin_addr = walker;

    memset(fragment + frag_begin_addr, 0, lba_size_);  // Ensure no stale bits.
    memcpy(fragment + frag_begin_addr + kTropoHeaderSize, ptr,
           fragment_length);  // new body.
    // Build header
    fragment[frag_begin_addr + 4] = static_cast<char>(fragment_length & 0xffu);
    fragment[frag_begin_addr + 5] =
        static_cast<char>((fragment_length >> 8) & 0xffu);
    fragment[frag_begin_addr + 6] =
        static_cast<char>((fragment_length >> 16) & 0xffu);
    fragment[frag_begin_addr + 7] = static_cast<char>(type);
    // CRC
    uint32_t crc = crc32c::Extend(type_crc_[static_cast<uint32_t>(type)], ptr,
                                  fragment_length);
    crc = crc32c::Mask(crc);
    EncodeFixed32(fragment + frag_begin_addr, crc);
    walker += fragment_length + kTropoHeaderSize;
    ptr += fragment_length;
    left -= fragment_length;
    begin = false;
  } while (s.ok() && left > 0);
  return s;
}

Status TropoCommitter::Commit(const Slice& data, uint64_t* lbas) {
  Status s = Status::OK();
  const char* ptr = data.data();
  size_t walker = 0;
  size_t left = data.size();

  size_t fragcount = data.size() / lba_size_ + 1;
  size_t size_needed = fragcount * kTropoHeaderSize + data.size();
  size_needed = ((size_needed + lba_size_ - 1) / lba_size_) * lba_size_;

  if (!(s = FromStatus(write_buffer_.ReallocBuffer(size_needed))).ok()) {
    TROPO_LOG_ERROR("Error: Commit: Failed resizing buffer\n");
    return s;
  }
  char* fragment;
  if (!(s = FromStatus(write_buffer_.GetBuffer((void**)&fragment))).ok()) {
    TROPO_LOG_ERROR("Error: Commit: Failed getting buffer\n");
    return s;
  }

  bool begin = true;
  uint64_t lbas_iter = 0;
  if (lbas != nullptr) {
    *lbas = 0;
  }
  do {
    // determine next fragment part.
    size_t avail = lba_size_;
    avail = avail > kTropoHeaderSize ? avail - kTropoHeaderSize : 0;
    const size_t fragment_length = (left < avail) ? left : avail;

    TropoRecordType type;
    const bool end = (left == fragment_length);
    if (begin && end) {
      type = TropoRecordType::kFullType;
    } else if (begin) {
      type = TropoRecordType::kFirstType;
    } else if (end) {
      type = TropoRecordType::kLastType;
    } else {
      type = TropoRecordType::kMiddleType;
    }
    size_t frag_begin_addr = 0;
    frag_begin_addr = walker;

    memset(fragment + frag_begin_addr, 0, lba_size_);  // Ensure no stale bits.
    memcpy(fragment + frag_begin_addr + kTropoHeaderSize, ptr,
           fragment_length);  // new body.
    // Build header
    fragment[frag_begin_addr + 4] = static_cast<char>(fragment_length & 0xffu);
    fragment[frag_begin_addr + 5] =
        static_cast<char>((fragment_length >> 8) & 0xffu);
    fragment[frag_begin_addr + 6] =
        static_cast<char>((fragment_length >> 16) & 0xffu);
    fragment[frag_begin_addr + 7] = static_cast<char>(type);
    // CRC
    uint32_t crc = crc32c::Extend(type_crc_[static_cast<uint32_t>(type)], ptr,
                                  fragment_length);
    crc = crc32c::Mask(crc);
    EncodeFixed32(fragment + frag_begin_addr, crc);
    walker += fragment_length + kTropoHeaderSize;
    ptr += fragment_length;
    left -= fragment_length;
    begin = false;
  } while (s.ok() && left > 0);

  s = FromStatus(
      log_->Append(write_buffer_, 0, size_needed, &lbas_iter, false));
  if (lbas != nullptr) {
    *lbas += lbas_iter;
  }
  if (!s.ok()) {
    TROPO_LOG_ERROR("Error: Commit: Fatal append error\n");
  }

  if (!keep_buffer_) {
    s = FromStatus(write_buffer_.FreeBuffer());
    if (!s.ok()) {
      TROPO_LOG_ERROR("Error: Commit: Failed freeing buffer\n");
    }
  }
  return s;
}

Status TropoCommitter::SafeCommit(const Slice& data, uint64_t* lbas) {
  if (!SpaceEnough(data)) {
    TROPO_LOG_ERROR("ERROR: Committer: No space left for Committer\n");
    return Status::IOError("No space left");
  }
  return Commit(data, lbas);
}

Status TropoCommitter::GetCommitReader(uint8_t reader_number, uint64_t begin,
                                       uint64_t end,
                                       TropoCommitReader* reader) {
  if (begin >= end || reader_number >= number_of_readers_) {
    return Status::InvalidArgument();
  }
  reader->commit_start = begin;
  reader->commit_end = end;
  reader->commit_ptr = reader->commit_start;
  reader->reader_nr = reader_number;
  reader->scratch = TropoDBConfig::deadbeef;
  if (!FromStatus(read_buffer_[reader->reader_nr]->ReallocBuffer(lba_size_))
           .ok()) {
    TROPO_LOG_ERROR("Error: Commit: Buffer memory limit\n");
    return Status::MemoryLimit();
  }

  return Status::OK();
}

bool TropoCommitter::SeekCommitReader(TropoCommitReader& reader,
                                      Slice* record) {
  // buffering issue
  if (read_buffer_[reader.reader_nr]->GetBufferSize() == 0) {
    TROPO_LOG_ERROR("ERROR: Commit: try to seek an undefined commit\n");
    return false;
  }
  if (reader.commit_ptr >= reader.commit_end) {
    return false;
  }
  reader.scratch.clear();
  record->clear();
  bool in_fragmented_record = false;

  while (reader.commit_ptr < reader.commit_end &&
         reader.commit_ptr >= reader.commit_start) {
    const size_t to_read =
        (reader.commit_end - reader.commit_ptr) * lba_size_ > lba_size_
            ? lba_size_
            : (reader.commit_end - reader.commit_ptr) * lba_size_;
    // first read header (prevents reading too much)
    log_->Read(reader.commit_ptr, *(&read_buffer_[reader.reader_nr]), 0,
               lba_size_, true, reader.reader_nr);
    // parse header
    const char* header;
    read_buffer_[reader.reader_nr]->GetBuffer((void**)&header);
    const uint32_t a = static_cast<uint32_t>(header[4]) & 0xff;
    const uint32_t b = static_cast<uint32_t>(header[5]) & 0xff;
    const uint32_t c = static_cast<uint32_t>(header[6]) & 0xff;
    const uint32_t d = static_cast<uint32_t>(header[7]);
    TropoRecordType type = d > kTropoRecordTypeLast
                               ? TropoRecordType::kInvalid
                               : static_cast<TropoRecordType>(d);
    const uint32_t length = a | (b << 8) | (c << 16);
    // read potential body
    if (length > lba_size_ && length <= to_read - kTropoHeaderSize) {
      read_buffer_[reader.reader_nr]->ReallocBuffer(to_read);
      read_buffer_[reader.reader_nr]->GetBuffer((void**)&header);
      // TODO: Could also skip first block, but atm addr is bugged.
      log_->Read(reader.commit_ptr, *(&read_buffer_[reader.reader_nr]), 0,
                 to_read, true, reader.reader_nr);
    }
    // TODO: we need better error handling at some point than setting to wrong
    // tag.
    if (kTropoHeaderSize + length > to_read) {
      type = TropoRecordType::kInvalid;
    }
    // Validate CRC
    {
      uint32_t expected_crc = crc32c::Unmask(DecodeFixed32(header));
      uint32_t actual_crc = crc32c::Value(header + 7, 1 + length);
      if (actual_crc != expected_crc) {
        TROPO_LOG_ERROR("ERROR: Seek commit: Corrupt crc %u %u %lu %lu\n",
                        length, d, reader.commit_ptr, reader.commit_end);
        type = TropoRecordType::kInvalid;
      }
    }
    reader.commit_ptr += (length + lba_size_ - 1) / lba_size_;
    switch (type) {
      case TropoRecordType::kFullType:
        reader.scratch.assign(header + kTropoHeaderSize, length);
        *record = Slice(reader.scratch);
        return true;
      case TropoRecordType::kFirstType:
        reader.scratch.assign(header + kTropoHeaderSize, length);
        in_fragmented_record = true;
        break;
      case TropoRecordType::kMiddleType:
        if (!in_fragmented_record) {
        } else {
          reader.scratch.append(header + kTropoHeaderSize, length);
        }
        break;
      case TropoRecordType::kLastType:
        if (!in_fragmented_record) {
        } else {
          reader.scratch.append(header + kTropoHeaderSize, length);
          *record = Slice(reader.scratch);
          return true;
        }
        break;
      default:
        in_fragmented_record = false;
        reader.scratch.clear();
        return false;
        break;
    }
  }
  return false;
}

bool TropoCommitter::CloseCommit(TropoCommitReader& reader) {
  if (!keep_buffer_) {
    read_buffer_[reader.reader_nr]->FreeBuffer();
  }
  reader.scratch.clear();
  return true;
}

Status TropoCommitter::GetCommitReaderString(std::string* in,
                                             TropoCommitReaderString* reader) {
  reader->commit_start = 0;
  reader->commit_end = in->size();
  reader->commit_ptr = reader->commit_start;
  reader->in = in;
  reader->scratch = TropoDBConfig::deadbeef;
  return Status::OK();
}

bool TropoCommitter::SeekCommitReaderString(TropoCommitReaderString& reader,
                                            Slice* record) {
  if (reader.commit_ptr >= reader.commit_end) {
    return false;
  }
  reader.scratch.clear();
  record->clear();
  bool in_fragmented_record = false;

  while (reader.commit_ptr < reader.commit_end &&
         reader.commit_ptr >= reader.commit_start) {
    const size_t to_read = (reader.commit_end - reader.commit_ptr) > lba_size_
                               ? lba_size_
                               : (reader.commit_end - reader.commit_ptr);
    // parse header
    const char* header = reader.in->data() + reader.commit_ptr;
    const uint32_t a = static_cast<uint32_t>(header[4]) & 0xff;
    const uint32_t b = static_cast<uint32_t>(header[5]) & 0xff;
    const uint32_t c = static_cast<uint32_t>(header[6]) & 0xff;
    const uint32_t d = static_cast<uint32_t>(header[7]);
    TropoRecordType type = d > kTropoRecordTypeLast
                               ? TropoRecordType::kInvalid
                               : static_cast<TropoRecordType>(d);
    const uint32_t length = a | (b << 8) | (c << 16);
    // TODO: we need better error handling at some point than setting to wrong
    // tag.
    if (kTropoHeaderSize + length > to_read) {
      type = TropoRecordType::kInvalid;
    }
    // Validate CRC
    {
      uint32_t expected_crc = crc32c::Unmask(DecodeFixed32(header));
      uint32_t actual_crc = crc32c::Value(header + 7, 1 + length);
      if (actual_crc != expected_crc) {
        TROPO_LOG_ERROR("Corrupt crc %u %u %lu %lu\n", length, d,
                        reader.commit_ptr, reader.commit_end);
        type = TropoRecordType::kInvalid;
      }
    }
    reader.commit_ptr +=
        ((length + kTropoHeaderSize + lba_size_ - 1) / lba_size_) * lba_size_;
    switch (type) {
      case TropoRecordType::kFullType:
        reader.scratch.assign(header + kTropoHeaderSize, length);
        *record = Slice(reader.scratch);
        return true;
      case TropoRecordType::kFirstType:
        reader.scratch.assign(header + kTropoHeaderSize, length);
        in_fragmented_record = true;
        break;
      case TropoRecordType::kMiddleType:
        if (!in_fragmented_record) {
        } else {
          reader.scratch.append(header + kTropoHeaderSize, length);
        }
        break;
      case TropoRecordType::kLastType:
        if (!in_fragmented_record) {
        } else {
          reader.scratch.append(header + kTropoHeaderSize, length);
          *record = Slice(reader.scratch);
          return true;
        }
        break;
      default:
        in_fragmented_record = false;
        reader.scratch.clear();
        return false;
        break;
    }
  }
  return false;
}

bool TropoCommitter::CloseCommitString(TropoCommitReaderString& reader) {
  reader.scratch.clear();
  return true;
}

}  // namespace ROCKSDB_NAMESPACE
