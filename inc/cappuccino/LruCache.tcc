#include "cappuccino/LockScopeGuard.h"
#include "cappuccino/LruCache.h"

#include <numeric>

namespace cappuccino {

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
LruCache<KeyType, ValueType, SyncType>::LruCache(
    size_t capacity,
    float max_load_factor)
    : m_elements(capacity)
    , m_keyed_elements(capacity)
    , m_lru_list(capacity)
{
    std::iota(m_lru_list.begin(), m_lru_list.end(), 0);
    m_lru_end = m_lru_list.begin();

    m_keyed_elements.max_load_factor(max_load_factor);
    m_keyed_elements.reserve(capacity);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::Insert(const KeyType& key, ValueType value) -> void
{
    LockScopeGuard<SyncType> guard { m_lock };
    doInsertUpdate(key, std::move(value));
};

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <template <class...> typename RangeType>
auto LruCache<KeyType, ValueType, SyncType>::InsertRange(
    RangeType<KeyValue> key_value_range) -> void
{
    LockScopeGuard<SyncType> guard { m_lock };
    for (auto& [key, value] : key_value_range) {
        doInsertUpdate(key, std::move(value));
    }
};

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <template <class...> typename RangeType, template <class, class> typename PairType>
auto LruCache<KeyType, ValueType, SyncType>::InsertRange(
    RangeType<PairType<KeyType, ValueType>> key_value_range) -> void
{
    LockScopeGuard<SyncType> guard { m_lock };
    for (auto& [key, value] : key_value_range) {
        doInsertUpdate(key, std::move(value));
    }
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::Delete(
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
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <template <class...> typename RangeType>
auto LruCache<KeyType, ValueType, SyncType>::DeleteRange(
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
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::Find(const KeyType& key) -> std::optional<ValueType>
{
    LockScopeGuard<SyncType> guard { m_lock };
    return doFind(key);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <template <class...> typename RangeType>
auto LruCache<KeyType, ValueType, SyncType>::FindRange(
    RangeType<KeyType, std::optional<ValueType>>& key_optional_value_range) -> void
{
    LockScopeGuard<SyncType> guard { m_lock };
    for (auto& [key, optional_value] : key_optional_value_range) {
        optional_value = doFind(key);
    }
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <template <class...> typename RangeType, template <class, class> typename PairType>
auto LruCache<KeyType, ValueType, SyncType>::FindRange(
    RangeType<PairType<KeyType, std::optional<ValueType>>>& key_optional_value_range) -> void
{
    LockScopeGuard<SyncType> guard { m_lock };
    for (auto& [key, optional_value] : key_optional_value_range) {
        optional_value = doFind(key);
    }
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::GetUsedSize() const -> size_t
{
    return m_used_size;
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::GetCapacity() const -> size_t
{
    return m_elements.size();
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::doInsertUpdate(
    const KeyType& key,
    ValueType&& value) -> void
{
    auto keyed_position = m_keyed_elements.find(key);
    if (keyed_position != m_keyed_elements.end()) {
        doUpdate(keyed_position, std::move(value));
    } else {
        doInsert(key, std::move(value));
    }
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::doInsert(
    const KeyType& key,
    ValueType&& value) -> void
{
    if (m_used_size >= m_elements.size()) {
        doPrune();
    }

    auto element_idx = *m_lru_end;

    auto keyed_position = m_keyed_elements.emplace(key, element_idx).first;

    Element& element = m_elements[element_idx];
    element.m_value = std::move(value);
    element.m_lru_position = m_lru_end;
    element.m_keyed_position = keyed_position;

    ++m_lru_end;

    ++m_used_size;

    // This is the most recently used item, put it in the appropriate place.
    doAccess(element);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::doUpdate(
    KeyedIterator keyed_position,
    ValueType&& value) -> void
{
    Element& element = m_elements[keyed_position->second];
    element.m_value = std::move(value);

    doAccess(element);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::doDelete(
    size_t element_idx) -> void
{
    Element& element = m_elements[element_idx];

    if (element.m_lru_position != std::prev(m_lru_end)) {
        m_lru_list.splice(m_lru_end, m_lru_list, element.m_lru_position);
    }
    --m_lru_end;

    m_keyed_elements.erase(element.m_keyed_position);

    --m_used_size;
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::doFind(
    const KeyType& key) -> std::optional<ValueType>
{
    auto keyed_position = m_keyed_elements.find(key);
    if (keyed_position != m_keyed_elements.end()) {
        size_t element_idx = keyed_position->second;
        Element& element = m_elements[element_idx];
        return { element.m_value };
    }

    return {};
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::doAccess(
    Element& element) -> void
{
    // Put the accessed item at the front of the LRU list.
    m_lru_list.splice(m_lru_list.begin(), m_lru_list, element.m_lru_position);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LruCache<KeyType, ValueType, SyncType>::doPrune() -> void
{
    if (m_used_size > 0) {
        doDelete(m_lru_list.back());
    }
}

} // namespace cappuccino
