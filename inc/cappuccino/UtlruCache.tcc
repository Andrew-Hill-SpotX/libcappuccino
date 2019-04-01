#include "cappuccino/LockScopeGuard.h"
#include "cappuccino/UtlruCache.h"

#include <numeric>

namespace cappuccino {

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
UtlruCache<KeyType, ValueType, SyncType>::UtlruCache(
    std::chrono::seconds ttl,
    size_t capacity,
    float max_load_factor)
    : m_ttl(ttl)
    , m_elements(capacity)
    , m_keyed_elements(capacity)
    , m_lru_list(capacity)
{
    std::iota(m_lru_list.begin(), m_lru_list.end(), 0);

    m_lru_end = m_lru_list.begin();

    m_keyed_elements.max_load_factor(max_load_factor);
    m_keyed_elements.reserve(capacity);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::Insert(
    const KeyType& key,
    ValueType value) -> void
{
    auto now = std::chrono::steady_clock::now();
    auto expire_time = now + m_ttl;

    LockScopeGuard<SyncType> guard { m_lock };
    doInsertUpdate(key, std::move(value), now, expire_time);
};

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <template <class...> typename RangeType>
auto UtlruCache<KeyType, ValueType, SyncType>::InsertRange(
    RangeType<KeyValue> key_value_range) -> void
{
    auto now = std::chrono::steady_clock::now();
    auto expire_time = now + m_ttl;

    LockScopeGuard<SyncType> guard { m_lock };
    for (auto& [key, value] : key_value_range) {
        doInsertUpdate(key, std::move(value), now, expire_time);
    }
};

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <template <class...> typename RangeType, template <class, class> typename PairType>
auto UtlruCache<KeyType, ValueType, SyncType>::InsertRange(
    RangeType<PairType<KeyType, ValueType>> key_value_range) -> void
{
    auto now = std::chrono::steady_clock::now();
    auto expire_time = now + m_ttl;

    LockScopeGuard<SyncType> guard { m_lock };
    for (auto& [key, value] : key_value_range) {
        doInsertUpdate(key, std::move(value), now, expire_time);
    }
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::Delete(
    const KeyType& key) -> bool
{
    LockScopeGuard<SyncType> guard { m_lock };
    auto keyed_position = m_keyed_elements.find(key);
    if (keyed_position != m_keyed_elements.end()) {
        doDelete(keyed_position->second);
        return true;
    } else {
        return false;
    }
};

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <template <class...> typename RangeType>
auto UtlruCache<KeyType, ValueType, SyncType>::DeleteRange(
    const RangeType<KeyType>& key_range) -> size_t
{
    size_t deleted_elements { 0 };

    LockScopeGuard<SyncType> guard { m_lock };

    for (auto& key : key_range) {
        auto keyed_position = m_keyed_elements.find(key);
        if (keyed_position != m_keyed_elements.end()) {
            ++deleted_elements;
            doDelete(keyed_position->second);
        }
    }

    return deleted_elements;
};

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::Find(
    const KeyType& key) -> std::optional<ValueType>
{
    auto now = std::chrono::steady_clock::now();

    LockScopeGuard<SyncType> guard { m_lock };
    return doFind(key, now);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <template <class...> typename RangeType>
auto UtlruCache<KeyType, ValueType, SyncType>::FindRange(
    RangeType<KeyType, std::optional<ValueType>>& key_optional_value_range) -> void
{
    auto now = std::chrono::steady_clock::now();

    LockScopeGuard<SyncType> guard { m_lock };

    for (auto& [key, optional_value] : key_optional_value_range) {
        optional_value = doFind(key, now);
    }
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <template <class...> typename RangeType, template <class, class> typename PairType>
auto UtlruCache<KeyType, ValueType, SyncType>::FindRange(
    RangeType<PairType<KeyType, ValueType>>& key_optional_value_range) -> void
{
    auto now = std::chrono::steady_clock::now();

    LockScopeGuard<SyncType> guard { m_lock };

    for (auto& [key, optional_value] : key_optional_value_range) {
        optional_value = doFind(key, now);
    }
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::GetUsedSize() const -> size_t
{
    return m_used_size;
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::GetCapacity() const -> size_t
{
    return m_elements.size();
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::CleanExpiredValues() -> size_t
{
    // how to determine when to stop cleaning since ttl list doesn't empty?

    size_t deleted_elements { 0 };
    auto now = std::chrono::steady_clock::now();

    {
        LockScopeGuard<SyncType> guard { m_lock };

        while (m_used_size > 0) {
            Element& element = m_elements[m_ttl_list.begin()];
            if (now >= element.m_expire_time) {
                ++deleted_elements;
                doDelete(m_ttl_list.begin());
            } else {
                break;
            }
        }
    }

    return deleted_elements;
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::doInsertUpdate(
    const KeyType& key,
    ValueType&& value,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point expire_time) -> void
{
    auto keyed_position = m_keyed_elements.find(key);
    if (keyed_position != m_keyed_elements.end()) {
        doUpdate(keyed_position, std::move(value), expire_time);
    } else {
        if (m_used_size >= m_elements.size()) {
            doPrune(now);
        }
        doInsert(key, std::move(value), expire_time);
    }
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::doInsert(
    const KeyType& key,
    ValueType&& value,
    std::chrono::steady_clock::time_point expire_time) -> void
{
    auto lru_position = m_lru_end;
    auto element_idx = *lru_position;

    auto keyed_position = m_keyed_elements.emplace(key, element_idx).first;

    m_ttl_list.emplace_back(element_idx);

    Element& element = m_elements[element_idx];
    element.m_value = std::move(value);
    element.m_expire_time = expire_time;
    element.m_lru_position = lru_position;
    element.m_ttl_position = std::prev(m_ttl_list.end());
    element.m_keyed_position = keyed_position;

    ++m_lru_end;

    ++m_used_size;
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::doUpdate(
    typename std::unordered_map<KeyType, size_t>::iterator keyed_position,
    ValueType&& value,
    std::chrono::steady_clock::time_point expire_time) -> void
{
    size_t element_idx = keyed_position->second;

    doAccess(element_idx);

    Element& element = m_elements[element_idx];
    element.m_expire_time = expire_time;

    // push to the end of the ttl list
    m_ttl_list.splice(m_ttl_list.end(), m_ttl_list, element.m_ttl_position);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::doDelete(
    size_t element_idx) -> void
{
    Element& element = m_elements[element_idx];

    if (element.m_lru_position != std::prev(m_lru_end)) {
        m_lru_list.splice(m_lru_end, m_lru_list, element.m_lru_position);
    }
    --m_lru_end;

    m_ttl_list.erase(element.m_ttl_position);

    m_keyed_elements.erase(element.m_keyed_position);

    --m_used_size;
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::doFind(
    const KeyType& key,
    std::chrono::steady_clock::time_point now) -> std::optional<ValueType>
{
    auto keyed_position = m_keyed_elements.find(key);
    if (keyed_position != m_keyed_elements.end()) {
        size_t element_idx = keyed_position->second;
        Element& element = m_elements[element_idx];

        // Has the element TTL'ed?
        if (now < element.m_expire_time) {
            doAccess(element_idx);
            return { element.m_value };
        } else {
            doDelete(element_idx);
        }
    }

    return {};
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::doAccess(
    size_t element_idx) -> void
{
    Element& element = m_elements[element_idx];
    m_lru_list.splice(m_lru_list.begin(), m_lru_list, element.m_lru_position);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto UtlruCache<KeyType, ValueType, SyncType>::doPrune(
    std::chrono::steady_clock::time_point now) -> void
{
    if (m_used_size > 0) {
        size_t ttl_idx = *m_ttl_list.begin();
        Element& element = m_elements[ttl_idx];

        if (now >= element.m_expire_time) {
            doDelete(ttl_idx);
        } else {
            size_t lru_idx = m_lru_list.back();
            doDelete(lru_idx);
        }
    }
}

} // namespace cappuccino
