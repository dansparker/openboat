#pragma once

// Type-safe, thread-safe publish/subscribe bus.
//
// Every data type (e.g. boat::core::Position) owns exactly one topic.
// Producers (sensor drivers, simulators) publish; consumers (UI, alerting,
// logging) either subscribe with a callback or poll the latest sample.
// The bus is the ONLY coupling between modules.

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace boat::core {

using Clock = std::chrono::steady_clock;
using SubscriptionId = std::uint64_t;

template <typename T>
struct Sample {
    T value{};
    Clock::time_point timestamp{};
};

// True if the sample is not older than max_age. Displays use this to
// red-X an instrument when its source goes stale.
template <typename T>
[[nodiscard]] bool is_fresh(const Sample<T>& sample, Clock::duration max_age,
                            Clock::time_point now = Clock::now()) {
    return now - sample.timestamp <= max_age;
}

class TopicBase {
public:
    virtual ~TopicBase();
};

template <typename T>
class Topic final : public TopicBase {
public:
    using Callback = std::function<void(const Sample<T>&)>;

    void publish(const T& value) { publish(Sample<T>{value, Clock::now()}); }

    void publish(const Sample<T>& sample) {
        std::vector<std::shared_ptr<Callback>> subscribers;
        {
            std::scoped_lock lock(mutex_);
            latest_ = sample;
            subscribers.reserve(subscribers_.size());
            for (const auto& [id, callback] : subscribers_) {
                subscribers.push_back(callback);
            }
        }
        // Callbacks run outside the lock so they may publish themselves.
        for (const auto& callback : subscribers) {
            (*callback)(sample);
        }
    }

    [[nodiscard]] std::optional<Sample<T>> latest() const {
        std::scoped_lock lock(mutex_);
        return latest_;
    }

    SubscriptionId subscribe(Callback callback) {
        std::scoped_lock lock(mutex_);
        const SubscriptionId id = next_id_++;
        subscribers_.emplace(id, std::make_shared<Callback>(std::move(callback)));
        return id;
    }

    void unsubscribe(SubscriptionId id) {
        std::scoped_lock lock(mutex_);
        subscribers_.erase(id);
    }

private:
    mutable std::mutex mutex_;
    std::optional<Sample<T>> latest_;
    std::map<SubscriptionId, std::shared_ptr<Callback>> subscribers_;
    SubscriptionId next_id_ = 1;
};

class DataBus {
public:
    DataBus() = default;
    DataBus(const DataBus&) = delete;
    DataBus& operator=(const DataBus&) = delete;

    // Returned reference stays valid for the lifetime of the bus.
    template <typename T>
    Topic<T>& topic() {
        std::scoped_lock lock(mutex_);
        auto& slot = topics_[std::type_index(typeid(T))];
        if (!slot) {
            slot = std::make_unique<Topic<T>>();
        }
        return static_cast<Topic<T>&>(*slot);
    }

    template <typename T>
    void publish(const T& value) {
        topic<T>().publish(value);
    }

    template <typename T>
    [[nodiscard]] std::optional<Sample<T>> latest() {
        return topic<T>().latest();
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::type_index, std::unique_ptr<TopicBase>> topics_;
};

}  // namespace boat::core
