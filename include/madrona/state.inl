/*
 * Copyright 2021-2022 Brennan Shacklett and contributors
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */
#pragma once

#include <madrona/utils.hpp>

#include <array>
#include <mutex>

namespace madrona {

template <typename ComponentT>
void ECSRegistry::registerComponent()
{
    state_mgr_->registerComponent<ComponentT>();
}

template <typename ArchetypeT>
void ECSRegistry::registerArchetype()
{
    state_mgr_->registerArchetype<ArchetypeT>();
}

template <typename SingletonT>
void ECSRegistry::registerSingleton()
{
    state_mgr_->registerSingleton<SingletonT>();
}

template <typename ArchetypeT, typename ComponentT>
void ECSRegistry::exportColumn(int32_t slot)
{
    (void)slot;
    (void)export_ptr_;
#if 0
    export_ptr_[slot] =
        state_mgr_->getArchetypeComponent<ArchetypeT, ComponentT>();
#endif
}

template <typename SingletonT>
void ECSRegistry::exportSingleton(int32_t slot)
{
    (void)slot;
#if 0
    export_ptr_[slot] = state_mgr_->getSingletonColumn<SingletonT>();
#endif
}

template <typename T>
T & EntityStore::LockedMapStore<T>::operator[](int32_t idx)
{
    return ((T *)store.data())[idx];
}

template <typename T>
const T & EntityStore::LockedMapStore<T>::operator[](int32_t idx) const
{
    return ((const T *)store.data())[idx];
}

Loc EntityStore::getLoc(Entity e) const
{
    return map_.lookup(e);
}

Loc EntityStore::getLocUnsafe(int32_t e_id) const
{
    return map_.getRef(e_id);
}

void EntityStore::setLoc(Entity e, Loc loc)
{
    map_.getRef(e) = loc;
}

void EntityStore::setRow(Entity e, uint32_t row)
{
    Loc &loc = map_.getRef(e);
    loc.row = row;
}

template <typename ComponentT>
ComponentID StateManager::registerComponent()
{
#ifdef MADRONA_MW_MODE
    std::lock_guard lock(register_lock_);

    uint32_t check_id = TypeTracker::typeID<ComponentT>();

    if (check_id < component_infos_.size() &&
        component_infos_[check_id].has_value()) {
        return ComponentID {
            check_id,
        };
    }
#endif

    TypeTracker::registerType<ComponentT>(&next_component_id_);

    uint32_t id = TypeTracker::typeID<ComponentT>();

    registerComponent(id, std::alignment_of_v<ComponentT>,
                      sizeof(ComponentT));

    return ComponentID {
        id,
    };
}

template <typename ArchetypeT>
ArchetypeID StateManager::registerArchetype()
{
#ifdef MADRONA_MW_MODE
    std::lock_guard lock(register_lock_);

    uint32_t check_id = TypeTracker::typeID<ArchetypeT>();

    if (check_id < archetype_stores_.size() &&
        archetype_stores_[check_id].has_value()) {
        return ArchetypeID {
            check_id,
        };
    }
#endif

    TypeTracker::registerType<ArchetypeT>(&next_archetype_id_);

    using Base = typename ArchetypeT::Base;

    using Delegator = utils::PackDelegator<Base>;

    auto archetype_components = Delegator::call([]<typename... Args>() {
        static_assert(std::is_same_v<Base, Archetype<Args...>>);
        uint32_t column_idx = user_component_offset_;

        auto registerColumnIndex =
                [&column_idx]<typename ComponentT>() {
            using LookupT = typename ArchetypeRef<ArchetypeT>::
                template ComponentLookup<ComponentT>;

            TypeTracker::registerType<LookupT>(&column_idx);
        };

        ( registerColumnIndex.template operator()<Args>(), ... );

        std::array archetype_components {
            ComponentID { TypeTracker::typeID<Args>() }
            ...
        };

        return archetype_components;
    });
    
    uint32_t id = TypeTracker::typeID<ArchetypeT>();

    registerArchetype(id,
        Span(archetype_components.data(), archetype_components.size()));

    return ArchetypeID {
        id,
    };
}


template <typename SingletonT>
void StateManager::registerSingleton()
{
    using ArchetypeT = SingletonArchetype<SingletonT>;

    registerComponent<SingletonT>();
    registerArchetype<ArchetypeT>();

#ifdef MADRONA_MW_MODE
    for (CountT i = 0; i < (CountT)num_worlds_; i++) {
        makeEntityNow<ArchetypeT>(uint32_t(i), init_state_cache_);
    }
#else
    makeEntityNow<ArchetypeT>(init_state_cache_);
#endif
}


template <typename SingletonT>
SingletonT & StateManager::getSingleton(MADRONA_MW_COND(uint32_t world_id))
{
    using ArchetypeT = SingletonArchetype<SingletonT>;
    uint32_t archetype_id = TypeTracker::typeID<ArchetypeT>();

    Table &tbl = 
#ifdef MADRONA_MW_MODE
        archetype_stores_[archetype_id]->tbls[world_id];
#else
        archetype_stores_[archetype_id]->tbl;
#endif

    return *(SingletonT *)tbl.data(user_component_offset_);
}

template <typename ComponentT>
ComponentID StateManager::componentID() const
{
    static_assert(!std::is_reference_v<ComponentT> &&
                  !std::is_pointer_v<ComponentT> &&
                  !std::is_const_v<ComponentT>);
    return ComponentID {
        TypeTracker::typeID<ComponentT>(),
    };
}

template <typename ArchetypeT>
ArchetypeID StateManager::archetypeID() const
{
    return ArchetypeID {
        TypeTracker::typeID<ArchetypeT>(),
    };
}

Loc StateManager::getLoc(Entity e) const
{
    return entity_store_.getLoc(e);
}

template <typename ComponentT>
inline ResultRef<ComponentT> StateManager::get(
    MADRONA_MW_COND(uint32_t world_id,) Loc loc)
{
    ArchetypeStore &archetype = *archetype_stores_[loc.archetype];
    Table &tbl =
#ifdef MADRONA_MW_MODE
        archetype.tbls[world_id];
#else
        archetype.tbl;
#endif

    auto col_idx = archetype.columnLookup.lookup(componentID<ComponentT>().id);

    if (!col_idx.has_value()) {
        return ResultRef<ComponentT>(nullptr);
    }

    return ResultRef<ComponentT>(
        (ComponentT *)tbl.data(*col_idx) + loc.row);
}

template <typename ComponentT>
ResultRef<ComponentT> StateManager::get(
    MADRONA_MW_COND(uint32_t world_id,) Entity entity)
{
    Loc loc = entity_store_.getLoc(entity);
    if (!loc.valid()) {
        return ResultRef<ComponentT>(nullptr);
    }

    return get<ComponentT>(MADRONA_MW_COND(world_id,) loc);
}

template <typename ComponentT>
ComponentT & StateManager::getUnsafe(
    MADRONA_MW_COND(uint32_t world_id,) int32_t entity_id)
{
    Loc loc = entity_store_.getLocUnsafe(entity_id);
    return getUnsafe<ComponentT>(MADRONA_MW_COND(world_id,) loc);
}

template <typename ComponentT>
ComponentT & StateManager::getUnsafe(
    MADRONA_MW_COND(uint32_t world_id,) Loc loc)
{
    ArchetypeStore &archetype = *archetype_stores_[loc.archetype];
    Table &tbl =
#ifdef MADRONA_MW_MODE
        archetype.tbls[world_id];
#else
        archetype.tbl;
#endif

    auto col_idx =
        *archetype.columnLookup.lookup(componentID<ComponentT>().id);

    return ((ComponentT *)tbl.data(col_idx))[loc.row];
}

template <typename ArchetypeT>
ArchetypeRef<ArchetypeT> StateManager::archetype(
    MADRONA_MW_COND(uint32_t world_id))
{
    auto archetype_id = archetypeID<ArchetypeT>();

    ArchetypeStore &archetype = *archetype_stores_[archetype_id.id];

    Table &tbl = 
#ifdef MADRONA_MW_MODE
        archetype.tbls[world_id];
#else
        archetype.tbl;
#endif

    return ArchetypeRef<ArchetypeT>(&tbl);
}

template <typename... ComponentTs>
Query<ComponentTs...> StateManager::query()
{
    std::array component_ids {
        componentID<std::remove_const_t<ComponentTs>>()
        ...
    };

    QueryRef *ref = &Query<ComponentTs...>::ref_;

    if (ref->numReferences.load(std::memory_order_acquire) == 0) {
        makeQuery(component_ids.data(), component_ids.size(), ref);
    }

    return Query<ComponentTs...>(true);
}

template <typename... ComponentTs, typename Fn>
void StateManager::iterateArchetypes(MADRONA_MW_COND(uint32_t world_id,)
                                     const Query<ComponentTs...> &query,
                                     Fn &&fn)
{
    using IndicesWrapper =
        std::make_integer_sequence<uint32_t, sizeof...(ComponentTs)>;

    iterateArchetypesImpl(MADRONA_MW_COND(world_id,)
                          query, std::forward<Fn>(fn), IndicesWrapper());
}

template <typename... ComponentTs, typename Fn, uint32_t... Indices>
void StateManager::iterateArchetypesImpl(MADRONA_MW_COND(uint32_t world_id,)
    const Query<ComponentTs...> &query, Fn &&fn,
    std::integer_sequence<uint32_t, Indices...>)
{
    assert(query.initialized_);

    uint32_t *cur_query_ptr = &query_state_.queryData[query.ref_.offset];
    const int num_archetypes = query.ref_.numMatchingArchetypes;

    for (int query_archetype_idx = 0; query_archetype_idx < num_archetypes;
         query_archetype_idx++) {
        uint32_t archetype_idx = *(cur_query_ptr++);

        ArchetypeStore &archetype = *archetype_stores_[archetype_idx];

        Table &tbl = 
#ifdef MADRONA_MW_MODE
            archetype.tbls[world_id];
#else
            archetype.tbl;
#endif

        int num_rows = tbl.numRows();

        fn(num_rows, (ComponentTs *)tbl.data(
            cur_query_ptr[Indices]) ...);

        cur_query_ptr += sizeof...(ComponentTs);
    }
}

template <typename... ComponentTs, typename Fn>
void StateManager::iterateEntities(MADRONA_MW_COND(uint32_t world_id,)
                                   const Query<ComponentTs...> &query, Fn &&fn)
{
    iterateArchetypes(MADRONA_MW_COND(world_id,) query, 
            [&fn](int num_rows, auto ...ptrs) {
        for (int i = 0; i < num_rows; i++) {
            fn(ptrs[i] ...);
        }
    });
}

template <typename ArchetypeT, typename... Args>
Entity StateManager::makeEntityNow(MADRONA_MW_COND(uint32_t world_id,)
                                   StateCache &cache, Args && ...args)
{
    ArchetypeID archetype_id = archetypeID<ArchetypeT>();

    ArchetypeStore &archetype = *archetype_stores_[archetype_id.id];

    Table &tbl =
#ifdef MADRONA_MW_MODE
        archetype.tbls[world_id];
#else
        archetype.tbl;
#endif

    constexpr uint32_t num_args = sizeof...(Args);

    assert((num_args == 0 || num_args == archetype.numComponents) &&
           "Trying to construct entity with wrong number of arguments");

    Entity e = entity_store_.newEntity(cache.entity_cache_);

    uint32_t new_row = tbl.addRow();

    ((Entity *)tbl.data(0))[new_row] = e;

    int component_idx = 0;

    auto constructNextComponent = [this, &component_idx, &archetype,
                                   &tbl, new_row](auto &&arg) {
        using ArgT = decltype(arg);
        using ComponentT = std::remove_reference_t<ArgT>;

        assert(componentID<ComponentT>().id ==
               archetype_components_[archetype.componentOffset +
                   component_idx].id);

        new ((ComponentT *)tbl.data(
                component_idx + user_component_offset_) + new_row)
            ComponentT(std::forward<ArgT>(arg));

        component_idx++;
    };

    ( constructNextComponent(std::forward<Args>(args)), ... );
    
    entity_store_.setLoc(e, Loc {
        .archetype = archetype_id.id,
        .row = int32_t(new_row),
    });

    return e;
}

template <typename ArchetypeT>
Loc StateManager::makeTemporary(MADRONA_MW_COND(uint32_t world_id))
{
    ArchetypeID archetype_id = archetypeID<ArchetypeT>();
    ArchetypeStore &archetype = *archetype_stores_[archetype_id.id];

    Table &tbl =
#ifdef MADRONA_MW_MODE
        archetype.tbls[world_id];
#else
        archetype.tbl;
#endif

    uint32_t new_row = tbl.addRow();

    return Loc {
        archetype_id.id,
        int32_t(new_row),
    };
}

template <typename ArchetypeT>
void StateManager::clear(MADRONA_MW_COND(uint32_t world_id,) StateCache &cache,
                         bool is_temporary)
{
    clear(MADRONA_MW_COND(world_id,) cache, archetypeID<ArchetypeT>().id,
          is_temporary);
}

#ifdef MADRONA_MW_MODE
uint32_t StateManager::numWorlds() const
{
    return num_worlds_;
}
#endif

}
