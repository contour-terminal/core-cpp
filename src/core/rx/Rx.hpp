#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace core::rx
{

struct Observer
{
    virtual void notify() = 0;
    virtual ~Observer() = default;
};

inline thread_local Observer* current_observer = nullptr;

class ObservableBase
{
    std::vector<Observer*> observers;

  public:
    void track()
    {
        if (current_observer)
        {
            if (std::find(observers.begin(), observers.end(), current_observer) == observers.end())
            {
                observers.push_back(current_observer);
            }
        }
    }

    void trigger()
    {
        auto copy = observers;
        for (auto* obs: copy)
        {
            obs->notify();
        }
    }
};

template <typename T>
class Value: public ObservableBase
{
    T val;

  public:
    Value(T v = T {}): val(std::move(v)) {}

    T get()
    {
        track();
        return val;
    }

    void set(T v)
    {
        val = std::move(v);
        trigger();
    }

    Value& operator=(T v)
    {
        set(std::move(v));
        return *this;
    }
    operator T() { return get(); }
};

template <typename T>
class Computed: public ObservableBase, public Observer
{
    std::function<T()> fn;
    T cached;
    bool dirty = true;

  public:
    Computed(std::function<T()> f): fn(std::move(f)) {}

    T get()
    {
        if (dirty)
        {
            auto* prev = current_observer;
            current_observer = this;
            cached = fn();
            current_observer = prev;
            dirty = false;
        }
        track();
        return cached;
    }

    void notify() override
    {
        dirty = true;
        trigger();
    }
};

class Effect: public Observer
{
    std::function<void()> fn;

  public:
    Effect(std::function<void()> f): fn(std::move(f)) { update(); }

    void update()
    {
        auto* prev = current_observer;
        current_observer = this;
        fn();
        current_observer = prev;
    }

    void notify() override { update(); }
};

} // namespace core::rx
