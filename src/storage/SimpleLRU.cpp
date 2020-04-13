#include "SimpleLRU.h"
#include <iostream>

namespace Afina {
namespace Backend {
bool SimpleLRU::TooBigPage(const std::string &key, const std::string &value) {
    return key.size() + value.size() > _max_size;
}

bool SimpleLRU::CacheIsFull() { return _current_size >= _max_size; }

bool SimpleLRU::QueueIsEmpty() { return _lru_head.get() == _lru_tail->prev; }

bool SimpleLRU::Delete_oldest_node() {
    if (QueueIsEmpty()) {
        return false;
    }
    lru_node *node_to_delete = _lru_head->next.get();
    _current_size -= node_to_delete->key.size() + node_to_delete->value.size();
    _lru_index.erase(node_to_delete->key);

    node_to_delete->next->prev = _lru_head.get();
    swap(node_to_delete->next, _lru_head->next);
    node_to_delete->next = nullptr;
    return true;
}

bool SimpleLRU::AddNode(const std::string &key, const std::string &value) {
    bool flag = true;
    while (flag && (CacheIsFull() || (key.size() + value.size()) > _max_size - _current_size)) {
        flag = Delete_oldest_node();
    }
    if (!flag) {
        return false;
    }

    auto temp = new lru_node{key, value, _lru_tail->prev, nullptr};
    temp->next = std::unique_ptr<lru_node>(temp);
    swap(temp->next, _lru_tail->prev->next);
    _lru_tail->prev = temp;

    _lru_index.emplace(std::reference_wrapper<const std::string>(temp->key), std::reference_wrapper<lru_node>(*temp));
    _current_size += key.size() + value.size();
    return true;
}

bool SimpleLRU::Refresh_node(lru_map::iterator &it, const std::string &value) {
    lru_node &node = it->second.get();
    // move the page to tail of queue
    if (node.next != nullptr) {
        node.next->prev = node.prev;
        swap(node.next, node.prev->next);
        node.prev = _lru_tail->prev;
        swap(node.next, _lru_tail->prev->next);
        _lru_tail->prev = &node;
    }
    bool flag = true;
    while (flag && ((value.size() - node.value.size()) > _max_size - _current_size)) {
        flag = Delete_oldest_node();
    }
    if (!flag) {
        return false;
    }
    _current_size -= node.value.size() - value.size();
    _lru_tail->prev->value = value;
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) {
    if (TooBigPage(key, value)) {
        return false;
    }
    auto it = _lru_index.find(std::reference_wrapper<const std::string>(key));
    if (it == _lru_index.end()) {
        return AddNode(key, value);
    } else {
        return Refresh_node(it, value);
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {
    auto it = _lru_index.find(std::reference_wrapper<const std::string>(key));
    if (it == _lru_index.end()) {
        return AddNode(key, value);
    } else {
        return false;
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) {
    if (TooBigPage(key, value)) {
        return false;
    }
    auto it = _lru_index.find(std::reference_wrapper<const std::string>(key));
    if (it == _lru_index.end()) {
        return false;
    } else {
        return Refresh_node(it, value);
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) {
    if (QueueIsEmpty()) {
        return false;
    }
    auto it = _lru_index.find(std::reference_wrapper<const std::string>(key));
    if (it == _lru_index.end()) {
        return false;
    }
    lru_node &node_to_delete = it->second.get();
    _lru_index.erase(it);
    _current_size -= key.size() + node_to_delete.value.size();

    node_to_delete.next->prev = node_to_delete.prev;
    node_to_delete.prev->next.swap(node_to_delete.next);
    node_to_delete.next.reset();
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) {
    auto it = _lru_index.find(std::reference_wrapper<const std::string>(key));
    if (it == _lru_index.end()) {
        return false;
    }
    value = it->second.get().value;
    return Refresh_node(it, value);
}

} // namespace Backend
} // namespace Afina