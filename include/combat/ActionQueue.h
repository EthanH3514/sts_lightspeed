//
// Created by gamerpuppy on 7/4/2021.
//

#ifndef STS_LIGHTSPEED_ACTIONQUEUE_H
#define STS_LIGHTSPEED_ACTIONQUEUE_H

#include "sts_common.h"

#include <vector>
#include <functional>
#include <cassert>


namespace sts {

    class BattleContext;
    typedef std::function<void (BattleContext &)> ActionFunction;

    struct Action {
        ActionFunction actFunc;
        bool clearOnCombatVictory = true;

        Action() = default;
        Action(ActionFunction a) : actFunc(std::move(a)) {}
        Action(ActionFunction a, bool b) : actFunc(std::move(a)), clearOnCombatVictory(b) {}
    };


    // Simple deque
    template<int capacity>
    struct ActionQueue {
        friend BattleContext;
        int front = 0;
        int back = 0;
        int size = 0;
        static_assert(capacity > 0, "queue initial capacity must be positive");
        std::vector<bool> bits = std::vector<bool>(capacity); // clear on victory
        std::vector<ActionFunction> arr = std::vector<ActionFunction>(capacity);

        // Capacity is storage, not a gameplay limit. Preserve both FIFO order and
        // victory-cleanup flags when a wrapped queue grows. Value copies own storage.
        void reserveOne() {
            if (size < getCapacity()) return;
            const int oldCapacity = getCapacity();
            std::vector<ActionFunction> grown(oldCapacity * 2);
            std::vector<bool> grownBits(oldCapacity * 2);
            for (int i = 0; i < size; ++i) {
                const int old = (front + i) % oldCapacity;
                grown[i] = std::move(arr[old]);
                grownBits[i] = bits[old];
            }
            arr.swap(grown);
            bits.swap(grownBits);
            front = 0;
            back = size;
        }

        void clear();
        void pushFront(Action a);
        void pushBack(Action a);
        bool isEmpty();
        ActionFunction popFront();
        [[nodiscard]] int getCapacity() const;
    };

    template<int capacity>
    void ActionQueue<capacity>::clear() {
        size = 0;
        back = 0;
        front = 0;
    }

    template <int capacity>
    void ActionQueue<capacity>::pushFront(Action a) {
        reserveOne();
        --front;
        ++size;
        if (front < 0) {
            front = getCapacity()-1;
        }
        bits[front] = a.clearOnCombatVictory;
        arr[front] = std::move(a.actFunc);
    }

    template<int capacity>
    void ActionQueue<capacity>::pushBack(Action a) {
        reserveOne();
        if (back >= getCapacity()) {
            back = 0;
        }
        bits[back] = a.clearOnCombatVictory;
        arr[back] = std::move(a.actFunc);
        ++back;
        ++size;
    }

    template<int capacity>
    bool ActionQueue<capacity>::isEmpty() {
        return size == 0;
    }

    template<int capacity>
    ActionFunction ActionQueue<capacity>::popFront() {
#ifdef sts_asserts
        assert(size > 0 );
#endif
        ActionFunction a = std::move(arr[front]);
        ++front;
        --size;
        if (front >= getCapacity()) {
            front = 0;
        }
        return a;
    }

    template<int capacity>
    int ActionQueue<capacity>::getCapacity() const {
        return static_cast<int>(arr.size());
    }

}


#endif //STS_LIGHTSPEED_ACTIONQUEUE_H
