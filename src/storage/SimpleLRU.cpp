#include "SimpleLRU.h"
#include <iostream>

namespace Afina {
namespace Backend {
bool SimpleLRU::TooBigPage(const std::string &key, const std::string &value) {
    return key.size() + value.size() > _max_size;
}

bool SimpleLRU::CacheIsFull() { return _current_size >= _max_size; }

bool SimpleLRU::QueueIsEmpty() { return _lru_tail == nullptr; }

void SimpleLRU::Delete_oldest_node() {
    if (QueueIsEmpty()) {
        return;
    }
    _current_size -= _lru_head->key.size() + _lru_head->value.size();
    _lru_index.erase(_lru_head->key);
    // if only one node in queue
    if (_lru_head == _lru_tail) {
        delete _lru_head;
        _lru_head = nullptr;
        _lru_tail = nullptr;
        return;
    }
    auto temp = _lru_head;
    _lru_head = _lru_head->next;
    if (_lru_head != nullptr) {
        _lru_head->prev = nullptr;
    }
    delete temp;
}

void SimpleLRU::AddNode(const std::string &key, const std::string &value) {
    while (CacheIsFull() || (key.size() + value.size()) > _max_size - _current_size) {
        Delete_oldest_node();
    }
    auto temp = new lru_node{key, value, _lru_tail, nullptr};

    // if it's the first page in the queue
    if (QueueIsEmpty()) {
        _lru_head = _lru_tail = temp;
    } else {
        _lru_tail->next = temp;
        _lru_tail = temp;
    }
    _lru_index.emplace(std::reference_wrapper<const std::string>(temp->key), std::reference_wrapper<lru_node>(*temp));
    _current_size += key.size() + value.size();
}

void SimpleLRU::Refresh_node(std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<lru_node>,
                                      std::less<std::string>>::iterator &it,
                             const std::string &value) {

    // move the page to tail of queue
    if (it->second.get().next != nullptr) {
        it->second.get().next->prev = it->second.get().prev;
        // if the node is the first in head
        if (it->second.get().prev == nullptr) {
            _lru_head = it->second.get().next;
            _lru_head->prev = nullptr;
        } else {
            it->second.get().prev->next = it->second.get().next;
        }
        it->second.get().prev = _lru_tail;
        it->second.get().next = nullptr;
        it->second.get().prev->next = &(it->second.get());
        _lru_tail = &(it->second.get());
    }
    _current_size -= it->second.get().value.size() - value.size();
    it->second.get().value = value;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) {
    if (TooBigPage(key, value)) {
        return false;
    }
    auto it = _lru_index.find(std::reference_wrapper<const std::string>(key));
    if (it == _lru_index.end()) {
        AddNode(key, value);
    } else {
        Refresh_node(it, value);
    }
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {
    auto it = _lru_index.find(std::reference_wrapper<const std::string>(key));
    if (it == _lru_index.end()) {
        AddNode(key, value);
    } else {
        return false;
    }
    return true;
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
        Refresh_node(it, value);
    }
    return true;
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
    _current_size -= key.size() + it->second.get().value.size();
    // if only one node in queue
    if (_lru_head == _lru_tail) {
        _lru_index.erase(key);
        delete _lru_head;
        _lru_head = nullptr;
        _lru_tail = nullptr;
        return true;
    }
    lru_node *node_to_delete;
    // if the node is the first in head
    if (it->second.get().prev == nullptr) {
        node_to_delete = _lru_head;
        _lru_head = it->second.get().next;
        _lru_head->prev = nullptr;
    } else {
        node_to_delete = it->second.get().prev->next;
        it->second.get().prev->next = it->second.get().next;
        if (it->second.get().next != nullptr) {
            it->second.get().next->prev = it->second.get().prev;
        } else {
            _lru_tail = it->second.get().prev;
        }
    }
    _lru_index.erase(key);
    delete node_to_delete;
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) {
    auto it = _lru_index.find(std::reference_wrapper<const std::string>(key));
    if (it == _lru_index.end()) {
        return false;
    }
    value = it->second.get().value;
    return true;
}

} // namespace Backend
} // namespace Afina