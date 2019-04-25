#include "cappuccino/LfuCache.h"
#include "cappuccino/LockScopeGuard.h"

#include <numeric>

namespace cappuccino {

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
LfuCache<KeyType, ValueType, SyncType>::LfuCache(
    size_t capacity,
    float max_load_factor)
    : m_elements(capacity)
    , m_open_list(capacity)
{
    std::iota(m_open_list.begin(), m_open_list.end(), 0);
    m_open_list_end = m_open_list.begin();

    m_keyed_elements.max_load_factor(max_load_factor);
    m_keyed_elements.reserve(capacity);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LfuCache<KeyType, ValueType, SyncType>::Insert(
    const KeyType& key, ValueType value) -> void
{
    LockScopeGuard<SyncType> guard { m_lock };
    doInsertUpdate(key, std::move(value));
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <typename RangeType>
auto LfuCache<KeyType, ValueType, SyncType>::InsertRange(
    RangeType&& key_value_range) -> void
{
    LockScopeGuard<SyncType> guard { m_lock };
    for (auto& [key, value] : key_value_range) {
        doInsertUpdate(key, std::move(value));
    }
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LfuCache<KeyType, ValueType, SyncType>::Delete(
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
auto LfuCache<KeyType, ValueType, SyncType>::DeleteRange(
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
auto LfuCache<KeyType, ValueType, SyncType>::Find(
    const KeyType& key, bool peek) -> std::optional<ValueType>
{
    LockScopeGuard<SyncType> guard { m_lock };
    return doFind(key, peek);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <typename RangeType>
auto LfuCache<KeyType, ValueType, SyncType>::FindRange(
    const RangeType& key_range,
    bool peek) -> std::vector<std::pair<KeyType, std::optional<ValueType>>>
{
    std::vector<std::pair<KeyType, std::optional<ValueType>>> output;
    output.reserve(std::size(key_range));

    {
        LockScopeGuard<SyncType> guard { m_lock };
        for (auto& key : key_range) {
            output.emplace_back(key, doFind(key, peek));
        }
    }

    return output;
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
template <typename RangeType>
auto LfuCache<KeyType, ValueType, SyncType>::FindRangeFill(
    RangeType& key_optional_value_range, bool peek) -> void
{
    LockScopeGuard<SyncType> guard { m_lock };
    for (auto& [key, optional_value] : key_optional_value_range) {
        optional_value = doFind(key, peek);
    }
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LfuCache<KeyType, ValueType, SyncType>::GetUsedSize() const -> size_t
{
    return m_used_size;
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LfuCache<KeyType, ValueType, SyncType>::GetCapacity() const -> size_t
{
    return m_elements.size();
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LfuCache<KeyType, ValueType, SyncType>::doInsertUpdate(
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
auto LfuCache<KeyType, ValueType, SyncType>::doInsert(
    const KeyType& key,
    ValueType&& value) -> void
{
    if (m_used_size >= m_elements.size()) {
        doPrune();
    }

    auto element_idx = *m_open_list_end;

    auto keyed_position = m_keyed_elements.emplace(key, element_idx).first;
    auto lfu_position = m_lfu_list.emplace(1, element_idx);

    Element& element = m_elements[element_idx];
    element.m_value = std::move(value);
    element.m_keyed_position = keyed_position;
    element.m_lfu_position = lfu_position;
    element.m_open_list_position = m_open_list_end;

    ++m_open_list_end;

    ++m_used_size;
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LfuCache<KeyType, ValueType, SyncType>::doUpdate(
    LfuCache::KeyedIterator keyed_position,
    ValueType&& value) -> void
{
    Element& element = m_elements[keyed_position->second];
    element.m_value = std::move(value);

    doAccess(element);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LfuCache<KeyType, ValueType, SyncType>::doDelete(
    size_t element_idx) -> void
{
    Element& element = m_elements[element_idx];

    if (element.m_open_list_position != std::prev(m_open_list_end)) {
        m_open_list.splice(m_open_list_end, m_open_list, element.m_open_list_position);
    }
    --m_open_list_end;

    m_keyed_elements.erase(element.m_keyed_position);
    m_lfu_list.erase(element.m_lfu_position);

    --m_used_size;
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LfuCache<KeyType, ValueType, SyncType>::doFind(
    const KeyType& key,
    bool peek) -> std::optional<ValueType>
{
    auto keyed_position = m_keyed_elements.find(key);
    if (keyed_position != m_keyed_elements.end()) {
        size_t element_idx = keyed_position->second;
        Element& element = m_elements[element_idx];
        // Don't update the elements access in the LRU if peeking.
        if (!peek) {
            doAccess(element);
        }
        return { element.m_value };
    }

    return {};
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LfuCache<KeyType, ValueType, SyncType>::doAccess(
    Element& element) -> void
{
    auto use_count = element.m_lfu_position->first;
    m_lfu_list.erase(element.m_lfu_position);
    element.m_lfu_position = m_lfu_list.emplace(use_count + 1, element.m_keyed_position->second);
}

template <typename KeyType, typename ValueType, SyncImplEnum SyncType>
auto LfuCache<KeyType, ValueType, SyncType>::doPrune() -> void
{
    if (m_used_size > 0) {
        doDelete(m_lfu_list.begin()->second);
    }
}

} // namespace cappuccino
