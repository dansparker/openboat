#include "boat/core/data_bus.hpp"
#include "boat/core/marine_data.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace boat::core;
using namespace std::chrono_literals;

TEST(DataBus, LatestIsEmptyBeforeFirstPublish) {
    DataBus bus;
    EXPECT_FALSE(bus.latest<Depth>().has_value());
}

TEST(DataBus, LatestReturnsLastPublishedValue) {
    DataBus bus;
    bus.publish(Depth{.below_transducer_m = 10.0});
    bus.publish(Depth{.below_transducer_m = 20.0});
    ASSERT_TRUE(bus.latest<Depth>().has_value());
    EXPECT_DOUBLE_EQ(bus.latest<Depth>()->value.below_transducer_m, 20.0);
}

TEST(DataBus, TopicsAreIsolatedByType) {
    DataBus bus;
    bus.publish(Depth{.below_transducer_m = 5.0});
    EXPECT_FALSE(bus.latest<AirData>().has_value());
}

TEST(DataBus, SubscribersReceiveSamples) {
    DataBus bus;
    std::vector<double> received;
    bus.topic<AirData>().subscribe(
        [&](const Sample<AirData>& s) { received.push_back(s.value.indicated_airspeed_kt); });

    bus.publish(AirData{.indicated_airspeed_kt = 90.0});
    bus.publish(AirData{.indicated_airspeed_kt = 95.0});

    ASSERT_EQ(received.size(), 2u);
    EXPECT_DOUBLE_EQ(received[1], 95.0);
}

TEST(DataBus, UnsubscribeStopsDelivery) {
    DataBus bus;
    int calls = 0;
    auto& topic = bus.topic<Depth>();
    const auto id = topic.subscribe([&](const Sample<Depth>&) { ++calls; });

    bus.publish(Depth{});
    topic.unsubscribe(id);
    bus.publish(Depth{});

    EXPECT_EQ(calls, 1);
}

TEST(DataBus, FreshnessCheck) {
    const auto now = Clock::now();
    const Sample<Depth> sample{Depth{}, now - 200ms};
    EXPECT_TRUE(is_fresh(sample, 500ms, now));
    EXPECT_FALSE(is_fresh(sample, 100ms, now));
}

TEST(DataBus, ConcurrentPublishersAndReaders) {
    DataBus bus;
    std::atomic<int> received{0};
    bus.topic<Depth>().subscribe([&](const Sample<Depth>&) { ++received; });

    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                bus.publish(Depth{.below_transducer_m = static_cast<double>(i)});
                (void)bus.latest<Depth>();
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_EQ(received.load(), kThreads * kPerThread);
}
