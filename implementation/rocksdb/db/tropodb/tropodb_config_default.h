#pragma once
#ifdef TROPODB_PLUGIN_ENABLED
#ifndef TROPODB_CONFIG_H
#define TROPODB_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include <limits>

#include "db/tropodb/utils/tropodb_logger.h"
#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {

/* Debug flag or perf related flags can be completely be removed from the binary
   if necesssary.
*/
// #define TROPICAL_DEBUG
// #define DISABLE_BACKGROUND_OPS // Used for e.g. WALPerfTest
// #define DISABLE_BACKGROUND_OPS_AND_RESETS // Used for e.g. WALPerfTest

// Changing any line here requires rebuilding all ZNS DB source files.
// Reasons for statics is static_asserts and as they can be directly used during
// compilation.
namespace TropoDBConfig {
// Versioning options
constexpr static size_t manifest_zones =
    4; /**< Amount of zones to reserve for metadata*/

// WAL options
constexpr static size_t zones_foreach_wal =
    4; /**< Amount of zones for each WAL*/
constexpr static bool wal_allow_buffering =
    true; /**< If writes are allowed to be buffered when written to the WAL.
             Increases performance at the cost of persistence.*/
constexpr static uint64_t wal_buffered_pages = 
    1; /**< Amount of pages that can be buffered. Increasing this will make 
            the system less reliable.*/
constexpr static bool wal_allow_group_commit =
    true; /**< Trade persistency for space and performance by grouping multiple KV-pairs
             in one page.*/
constexpr static size_t wal_count = 40; /**< Amount of WALs on one zone region*/
constexpr static bool wal_unordered = true; /**< WAL appends can be reordered */
constexpr static uint8_t wal_iodepth =
    4; /**< Determines the outstanding queue depth for each WAL. */
constexpr static bool wal_preserve_dma =
    true; /**< Some DMA memory is claimed for WALs, even WALs are not busy.
             Prevents reallocations. */

// L0 and LN options
constexpr static uint8_t level_count =
    6; /**< Amount of LSM-tree levels L0 up to LN */
constexpr static size_t L0_zones =
    100; /**< amount of zones to reserve for each L0 circular log */
constexpr static uint8_t lower_concurrency =
    1; /**< Number of L0 circular logs. Increases parallelism. */
constexpr static size_t wal_manager_zone_count = wal_count / lower_concurrency;
constexpr static int L0_slow_down =
    80; /**< Amount of SSTables in L0 before client puts will be stalled. Can
           stabilise latency. Setting this too high can cause some clients to
           wait for minutes during heavy background I/O.*/
static constexpr uint8_t number_of_concurrent_L0_readers =
    4;  // Maximum number of concurrent reader threads reading from L0.
static constexpr uint8_t number_of_concurrent_LN_readers =
    4;  // Maximum number of concurrent reader threads reading from LN.
constexpr static size_t min_ss_zone_count =
    5; /**< Minimum amount of zones for L0 and LN each*/
constexpr static double ss_compact_treshold[level_count]{
    8.,
    16. * 1024. * 1024. * 1024.,
    16. * 4. * 1024. * 1024. * 1024.,
    16. * 16. * 1024. * 1024. * 1024.,
    16. * 64. * 1024. * 1024. * 1024.,
    16. * 256. * 1024. * 1024. * 1024.}; /**< Size of each level before we want
compaction. L0 is in number of SSTables, L1 and up in bytes. */
constexpr static double ss_compact_treshold_force[level_count]{
    0.85, 0.95, 0.95, 0.95,
    0.95, 0.95}; /**< A level can containd live and dead tables and this can be
                    more than the treshold. However, at some point in time it is
                    full and compaction is FORCED. This treshold prevents out of
                    space issues. E.g. L0 may not be filled more than 85%.*/
constexpr static double ss_compact_modifier[level_count]{
    64, 32, 16, 8,
    4,  1}; /**< Modifier for each compaction treshold. When size reaches above
              the compaction treshold, certain compactions might be more
              important than others. For example, setting a high modifier for 2
              causes compactions in 2 to be done first. 0 and 1 are ignored as
              they are handled in a separate thread */
constexpr static uint64_t max_bytes_sstable_l0 =
    1024U * 1024U * 512; /**< Maximum size of SSTables in L0. Determines the
                amount of tables generated on a flush. Rounded to round number
                of lbas by TropoDB. */
constexpr static uint64_t max_bytes_sstable_ =
    (uint64_t)(1073741824. * 2. *
               0.95); /**< Maximum size of SSTables in LN. LN tables
                         reserve entire zones, therefore, please set
                         to a multitude of approximately n zones */
constexpr static uint64_t max_lbas_compaction_l0 = 2097152 * 12; /**< Maximum
amount of LBAS that can be considered for L0 to LN compaction. Prevents OOM.*/

// Flushes
constexpr static bool flushes_allow_deferring_writes =
    true; /**< Allows deferring SSTable writes during flushes to a separate
             thread. This can allow flushing to continue without waiting on
             writes to finish.*/
constexpr static uint8_t flushing_maximum_deferred_writes =
    4; /**< How many SSTables can be deferred at most. Be careful, setting
this too high can cause OOM.*/

// Compaction
constexpr static bool compaction_allow_prefetching =
    true; /**< If LN tables can be prefetched during compaction. This uses one
             thread more and some RAM, but can reduce compaction time. */
constexpr static uint8_t compaction_maximum_prefetches =
    6; /**< How many SSTables can be prefetched at most. Be careful, setting
          this too high can cause OOM.*/
constexpr static bool compaction_allow_deferring_writes =
    true; /**< Allows deferring SSTable writes during compaction to a separate
             thread. This can allow compaction to continue without waiting on
             writes to finish.*/
constexpr static uint8_t compaction_maximum_deferred_writes =
    6; /**< How many SSTables can be deferred at most. Be careful, setting
this too high can cause OOM.*/
constexpr static uint64_t compaction_max_grandparents_overlapping_tables =
    10; /**< Maximum number of tables that are allowed to overlap with
           grandparent */

// Containerisation
constexpr static uint64_t min_zone = 0; /**< Minimum zone to use for database.*/
constexpr static uint64_t max_zone =
    0x0; /**< Maximum zone to use for database. Set to 0 for full region*/

// MISC
constexpr static TropoLogLevel default_log_level =
    TropoLogLevel::TROPO_INFO_LEVEL;
constexpr static size_t max_channels =
    0x100; /**< Maximum amount of channels that can be live. Used to ensure
              that there is no channel leak. */
constexpr static bool use_sstable_encoding =
    true; /**< If RLE should be used for SSTables. */
constexpr static uint32_t max_sstable_encoding = 16; /**< RLE max size. */
constexpr static const char* deadbeef =
    "\xaf\xeb\xad\xde"; /**< Used for placeholder strings*/

// Configs are asking for trouble... As they say in security, never trust user
// input! Even/especially your own.
static_assert(level_count > 1 &&
              level_count <
                  std::numeric_limits<uint8_t>::max() -
                      1);  // max - 1 because we sometimes poll at next level.
                           // We do not want numeric overflows...
static_assert(manifest_zones > 1);
static_assert(zones_foreach_wal > 2);
static_assert((wal_allow_buffering && wal_buffered_pages > 0) || 
    (!wal_allow_buffering && wal_buffered_pages==0));
static_assert(!wal_allow_group_commit || wal_allow_buffering);
static_assert(wal_count > 2);
static_assert(wal_unordered || wal_iodepth == 1,
              "WAL io_depth of more than 1 requires unordered writes");
static_assert(L0_slow_down > 0);
static_assert(number_of_concurrent_L0_readers > 0);
static_assert(number_of_concurrent_LN_readers > 0);
static_assert(min_ss_zone_count > 1);
static_assert(sizeof(ss_compact_treshold) == level_count * sizeof(double));
static_assert(sizeof(ss_compact_treshold_force) ==
              level_count * sizeof(double));
static_assert(sizeof(ss_compact_modifier) == level_count * sizeof(double));
static_assert(((max_zone == min_zone) && min_zone == 0) || min_zone < max_zone);
static_assert(max_zone == 0 || max_zone > manifest_zones +
                                              zones_foreach_wal * wal_count +
                                              min_ss_zone_count * level_count);
static_assert(max_bytes_sstable_l0 > 0);
static_assert(max_bytes_sstable_ > 0);
static_assert(max_lbas_compaction_l0 > 0);
static_assert((!flushes_allow_deferring_writes &&
               flushing_maximum_deferred_writes == 0) ||
              (flushes_allow_deferring_writes &&
               flushing_maximum_deferred_writes > 0));
static_assert(
    (!compaction_allow_prefetching && compaction_maximum_prefetches == 0) ||
    (compaction_allow_prefetching && compaction_maximum_prefetches > 0));
static_assert((!compaction_allow_deferring_writes &&
               compaction_maximum_deferred_writes == 0) ||
              (compaction_allow_deferring_writes &&
               compaction_maximum_deferred_writes > 0));
static_assert(max_lbas_compaction_l0 > 0);
static_assert(max_channels > 0);
static_assert(!use_sstable_encoding || max_sstable_encoding > 0);
#ifndef TROPICAL_DEBUG
static_assert(default_log_level > TropoLogLevel::TROPO_DEBUG_LEVEL,
              "Debug level can not be set to debug when debug is disabled");
#endif
}  // namespace TropoDBConfig
}  // namespace ROCKSDB_NAMESPACE
#endif
#endif
