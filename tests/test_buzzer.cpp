#include "boat/buzzer/buzzer.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "boat/core/data_bus.hpp"

using namespace boat;

namespace {

class FakeOutput final : public buzzer::Output {
public:
    explicit FakeOutput(std::atomic<int>& switches) : switches_(switches) {}
    void set(bool on) override {
        if (on) ++switches_;
    }

private:
    std::atomic<int>& switches_;
};

}  // namespace

TEST(BuzzerModule, StartupBeepProvesTheBuzzerWorks) {
    std::atomic<int> beeps{0};
    core::DataBus bus;
    buzzer::BuzzerModule m(std::make_unique<FakeOutput>(beeps), true);
    m.start(bus);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    m.stop();
    EXPECT_EQ(beeps.load(), 1);
}
