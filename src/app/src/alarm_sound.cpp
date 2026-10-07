#include "alarm_sound.hpp"

#include <QByteArray>
#include <QDataStream>
#include <QTemporaryFile>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

#include "boat/nav/nav.hpp"

#ifdef BOAT_HAVE_MULTIMEDIA
#include <QSoundEffect>
#endif

namespace {

// 0.2 s beep at 2.8 kHz (where hearing is most sensitive), soft edges against clicks
[[maybe_unused]] QByteArray beep_wav() {
    constexpr int kRate = 22050;
    constexpr int kSamples = kRate / 5;
    QByteArray pcm;
    QDataStream out(&pcm, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    for (int i = 0; i < kSamples; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double edge = std::min({1.0, i / 200.0, (kSamples - i) / 200.0});
        const double v = std::sin(2 * std::numbers::pi * 2800.0 * t) * edge * 0.8;
        out << static_cast<qint16>(v * 32767);
    }
    QByteArray wav;
    QDataStream w(&wav, QIODevice::WriteOnly);
    w.setByteOrder(QDataStream::LittleEndian);
    const auto size = static_cast<quint32>(pcm.size());
    w.writeRawData("RIFF", 4);
    w << quint32(36 + size);
    w.writeRawData("WAVEfmt ", 8);
    w << quint32(16) << quint16(1) << quint16(1) << quint32(kRate) << quint32(kRate * 2) << quint16(2) << quint16(16);
    w.writeRawData("data", 4);
    w << size;
    wav.append(pcm);
    return wav;
}

}  // namespace

bool AlarmSound::available() {
#ifdef BOAT_HAVE_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

AlarmSound::AlarmSound(boat::core::DataBus& bus, QObject* parent)
    : QObject(parent), bus_(bus), t0_(std::chrono::steady_clock::now()) {
#ifdef BOAT_HAVE_MULTIMEDIA
    wav_ = std::make_unique<QTemporaryFile>(QStringLiteral("openboat-beep-XXXXXX.wav"));
    if (wav_->open()) {
        wav_->write(beep_wav());
        wav_->flush();
        effect_ = new QSoundEffect(this);
        effect_->setSource(QUrl::fromLocalFile(wav_->fileName()));
        effect_->setVolume(1.0);
    }
#endif
    connect(&timer_, &QTimer::timeout, this, &AlarmSound::tick);
    timer_.start(25);
}

AlarmSound::~AlarmSound() = default;

void AlarmSound::tick() {
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
    std::optional<boat::nav::AlarmLevel> level;
    if (t > 5.0) {  // give the nav module time to publish its first list
        const auto list = bus_.latest<boat::nav::AlarmList>();
        // No fresh list = alarm monitoring stopped: silence would look like "all ok"
        level = list && boat::core::is_fresh(*list, std::chrono::seconds(5))
                    ? list->value.sound
                    : std::optional(boat::nav::AlarmLevel::Alarm);
    }
    const bool on = boat::nav::buzzer_on(level, t);
#ifdef BOAT_HAVE_MULTIMEDIA
    if (on && !last_ && effect_ != nullptr) effect_->play();
#endif
    last_ = on;
}
