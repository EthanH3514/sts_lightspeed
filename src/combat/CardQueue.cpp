//
// Created by gamerpuppy on 8/24/2021.
//

#include "combat/CardQueue.h"

using namespace sts;


bool CardQueue::isEmpty() const {
    return size == 0;
}

bool CardQueue::containsCardWithId(int uniqueId) const {
    for (int i = 0; i < size; ++i) {
        const int idx = (frontIdx + i) % arr.size();
        if (arr[idx].card.getUniqueId() == uniqueId) {
            return true;
        }
    }
    return false;
}

void CardQueue::reserveOne() {
    if (size < static_cast<int>(arr.size())) return;
    std::vector<CardQueueItem> grown(arr.size() * 2);
    for (int i = 0; i < size; ++i)
        grown[i] = std::move(arr[(frontIdx + i) % arr.size()]);
    arr.swap(grown);
    frontIdx = 0;
    backIdx = size;
}

void CardQueue::clear() {
    size = 0;
    backIdx = 0;
    frontIdx = 0;
}

void CardQueue::pushFront(CardQueueItem item) {
    reserveOne();
    --frontIdx;
    ++size;
    if (frontIdx < 0) {
        frontIdx = arr.size() - 1;
    }
    arr.at(frontIdx) = std::move(item);
}

void CardQueue::pushBack(CardQueueItem item) {
    reserveOne();
    arr.at(backIdx) = std::move(item);
    ++backIdx;
    ++size;
    if (backIdx >= static_cast<int>(arr.size())) {
        backIdx = 0;
    }
}

CardQueueItem CardQueue::popFront() {
    assert(size > 0);
    CardQueueItem &item = arr.at(frontIdx);
    ++frontIdx;
    --size;
    if (frontIdx >= static_cast<int>(arr.size())) {
        frontIdx = 0;
    }
    return item;
}

CardQueueItem CardQueue::popBack() {
    assert(size > 0);
    --backIdx;
    --size;
    if (backIdx < 0) {
        backIdx = arr.size() - 1;
    }
    CardQueueItem &item = arr.at(backIdx);
    return item;
}

CardQueueItem &CardQueue::front() {
    assert(size > 0);
    return arr.at(frontIdx);
}
