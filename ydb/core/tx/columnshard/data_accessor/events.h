#pragma once

#include "abstract/collector.h"

#include <ydb/core/tx/columnshard/columnshard_private_events.h>
#include <ydb/core/tx/columnshard/common/path_id.h>
#include <ydb/core/tx/columnshard/engines/portions/data_accessor.h>

#include <ydb/library/accessor/accessor.h>
#include <ydb/library/actors/core/event_local.h>

namespace NKikimr::NOlap {
class IGranuleDataAccessor;
class TDataAccessorsRequest;
}   // namespace NKikimr::NOlap

namespace NKikimr::NOlap::NDataAccessorControl {

class TEvAddPortion: public NActors::TEventLocal<TEvAddPortion, NColumnShard::TEvPrivate::EEv::EvAddPortionDataAccessor> {
private:
    std::vector<TPortionDataAccessor> Accessors;
    YDB_READONLY_DEF(TActorId, Owner);

public:
    std::vector<TPortionDataAccessor> ExtractAccessors() {
        return std::move(Accessors);
    }

    explicit TEvAddPortion(const TPortionDataAccessor& accessor, TActorId owner)
        : Accessors({accessor})
        , Owner(owner)
    {
    }

    explicit TEvAddPortion(const std::vector<TPortionDataAccessor>& accessors, TActorId owner)
        : Accessors(accessors)
        , Owner(owner)
    {
    }
};

class TEvRemovePortion: public NActors::TEventLocal<TEvRemovePortion, NColumnShard::TEvPrivate::EEv::EvRemovePortionDataAccessor> {
private:
    YDB_READONLY_DEF(TPortionInfo::TConstPtr, Portion);
    YDB_READONLY_DEF(TActorId, Owner);

public:
    explicit TEvRemovePortion(const TPortionInfo::TConstPtr& portion, TActorId owner)
        : Portion(portion)
        , Owner(owner)
    {
    }
};

class TEvRegisterController: public NActors::TEventLocal<TEvRegisterController, NColumnShard::TEvPrivate::EEv::EvRegisterGranuleDataAccessor> {
private:
    std::unique_ptr<IGranuleDataAccessor> Controller;
    YDB_READONLY_DEF(bool, IsUpdateFlag);
    YDB_READONLY_DEF(TActorId, Owner);


public:
    std::unique_ptr<IGranuleDataAccessor> ExtractController() {
        return std::move(Controller);
    }

    explicit TEvRegisterController(std::unique_ptr<IGranuleDataAccessor>&& accessor, const bool isUpdate, TActorId owner)
        : Controller(std::move(accessor))
        , IsUpdateFlag(isUpdate)
        , Owner(owner)
    {
    }
};

class TEvUnregisterController
    : public NActors::TEventLocal<TEvUnregisterController, NColumnShard::TEvPrivate::EEv::EvUnregisterGranuleDataAccessor> {
private:
    YDB_READONLY_DEF(TInternalPathId, PathId);
    YDB_READONLY_DEF(TActorId, Owner);

public:
    explicit TEvUnregisterController(const TInternalPathId pathId, TActorId owner)
        : PathId(pathId)
        , Owner(owner)
    {
    }
};

class TEvClearCache
    : public NActors::TEventLocal<TEvClearCache, NColumnShard::TEvPrivate::EEv::EvClearCacheDataAccessor> {
private:
    YDB_READONLY_DEF(TActorId, Owner);

public:
    explicit TEvClearCache(TActorId owner) : Owner(owner) {}
};


class TEvAskTabletDataAccessors
    : public NActors::TEventLocal<TEvAskTabletDataAccessors, NColumnShard::TEvPrivate::EEv::EvAskTabletDataAccessors> {
private:
    using TPortions = THashMap<TInternalPathId, TPortionsByConsumer>;
    YDB_ACCESSOR_DEF(TPortions, Portions);
    YDB_READONLY_DEF(std::shared_ptr<NDataAccessorControl::IAccessorCallback>, Callback);

public:
    explicit TEvAskTabletDataAccessors(TPortions&& portions, const std::shared_ptr<NDataAccessorControl::IAccessorCallback>& callback)
        : Portions(std::move(portions))
        , Callback(callback) {
    }
};

class TEvAskServiceDataAccessors
    : public NActors::TEventLocal<TEvAskServiceDataAccessors, NColumnShard::TEvPrivate::EEv::EvAskServiceDataAccessors> {
private:
    YDB_READONLY_DEF(std::shared_ptr<TDataAccessorsRequest>, Request);
    YDB_READONLY_DEF(TActorId, Owner);

public:
    explicit TEvAskServiceDataAccessors(const std::shared_ptr<TDataAccessorsRequest>& request, TActorId owner)
        : Request(request)
        , Owner(owner)
    {
    }
};

}   // namespace NKikimr::NOlap::NDataAccessorControl
