#pragma once
#include "read.h"
#include "remove_declare.h"
#include "remove_gc.h"
#include "write.h"

#include <ydb/library/signals/owner.h>

#include <library/cpp/monlib/dynamic_counters/counters.h>
#include <util/generic/hash.h>

namespace NKikimr::NOlap::NBlobOperations {

class TStorageCounters;

enum class EConsumer {
    TTL = 0,
    GENERAL_COMPACTION,
    INDEXATION,
    CLEANUP_TABLES,
    CLEANUP_PORTIONS,
    CLEANUP_INSERT_TABLE,
    CLEANUP_SHARED_BLOBS,
    EXPORT,
    SCAN,
    SYS_VIEW_SCAN,
    GC,
    WRITING,
    WRITING_BUFFER,
    WRITING_OPERATOR,
    NORMALIZER,
    STATISTICS,
    STATISTICS_FLUSH,
    FETCH_ON_LOAD,
    UNDEFINED,

    COUNT
};

class TConsumerCounters: public NColumnShard::TCommonCountersOwner {
private:
    using TBase = NColumnShard::TCommonCountersOwner;
    YDB_READONLY_DEF(std::shared_ptr<TReadCounters>, ReadCounters);
    YDB_READONLY_DEF(std::shared_ptr<TWriteCounters>, WriteCounters);
    YDB_READONLY_DEF(std::shared_ptr<TRemoveDeclareCounters>, RemoveDeclareCounters);
    YDB_READONLY_DEF(std::shared_ptr<TRemoveGCCounters>, RemoveGCCounters);

public:
    TConsumerCounters(const TString& consumerId, const TStorageCounters& parent);
};

class TStorageCounters: public NColumnShard::TCommonCountersOwner {
private:
    using TBase = NColumnShard::TCommonCountersOwner;
    std::vector<std::shared_ptr<TConsumerCounters>> Consumers;

public:
    TStorageCounters(const TString& storageId);

    std::shared_ptr<TConsumerCounters> GetConsumerCounter(const EConsumer consumer);
};

}   // namespace NKikimr::NOlap::NBlobOperations
