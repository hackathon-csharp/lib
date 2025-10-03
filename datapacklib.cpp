#if 0
#include "datapacklib.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace datapack
{

namespace
{
static inline long maxLong(long a, long b)
{
    return (a > b) ? a : b;
}

static inline double maxDouble(double a, double b)
{
    return (a > b) ? a : b;
}

static const std::uint16_t CRC_POLY = 0x1021;
static const std::uint16_t CRC_INIT = 0xFFFF;

static const LightLevel SYMBOL_TO_COLOR[4] = {
    LightLevel::Red,
    LightLevel::Green,
    LightLevel::Blue,
    LightLevel::White
};

static bool colorToSymbol(LightLevel level, std::uint8_t& symbol)
{
    switch (level)
    {
    case LightLevel::Red:
        symbol = 0;
        return true;
    case LightLevel::Green:
        symbol = 1;
        return true;
    case LightLevel::Blue:
        symbol = 2;
        return true;
    case LightLevel::White:
        symbol = 3;
        return true;
    default:
        return false;
    }
}

static std::uint16_t computeCrc16(const std::uint8_t* data, std::size_t size)
{
    std::uint16_t crc = CRC_INIT;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8U;
        for (int bit = 0; bit < 8; ++bit)
        {
            if ((crc & 0x8000U) != 0)
            {
                crc = static_cast<std::uint16_t>((crc << 1U) ^ CRC_POLY);
            }
            else
            {
                crc <<= 1U;
            }
        }
    }
    return crc;
}

class SignalWriter
{
public:
    SignalWriter(long unitDuration, SignalBuffer& target, bool& ok)
        : unitDuration_(unitDuration), target_(target), ok_(ok)
    {
    }

    void emit(LightLevel level, long units)
    {
        if (!ok_ || units <= 0)
        {
            return;
        }
        const long duration = units * unitDuration_;
        SignalChange change;
        change.level = level;
        change.duration = duration;
        if (!target_.push_back(change))
        {
            ok_ = false;
        }
    }

private:
    long unitDuration_;
    SignalBuffer& target_;
    bool& ok_;
};

} // namespace

long ProtocolConfig::tolerance(long expectedUnits) const
{
    const double fraction = maxDouble(allowedDriftFraction, 0.01);
    const double raw = ceil(static_cast<double>(expectedUnits) * fraction);
    return maxLong(1, static_cast<long>(raw));
}
Encoder::Encoder(const ProtocolConfig& config)
{
    configure(config);
}

const ProtocolConfig& Encoder::config() const noexcept
{
    return config_;
}

Decoder::Decoder(DataCallback callback, ProtocolConfig config)
    : config_(config), callback_(std::move(callback))
{
    if (config_.unitDurationMicros <= 0)
    {
        throw std::invalid_argument("unitDurationMicros must be positive");
    }
    if (config_.symbolMarkUnits <= 0 || config_.separatorUnits <= 0)
    {
        throw std::invalid_argument("symbol and separator units must be positive");
    }
    if (config_.preambleMarkUnits <= 0 || config_.preambleSpaceUnits <= 0)
    {
        throw std::invalid_argument("preamble units must be positive");
    }
    reset();
}

void Decoder::setCallback(DataCallback callback)
{
    callback_ = std::move(callback);
}

const ProtocolConfig& Decoder::config() const noexcept
{
    return config_;
}

const DecoderStats& Decoder::stats() const noexcept
{
    return stats_;
}

void Decoder::reset()
{
    state_ = State::Idle;
    frameBuffer_.clear();
    currentByte_ = 0;
    bitsFilled_ = 0;
    expectedPayloadLength_ = 0;
    payloadLengthKnown_ = false;
    pendingSymbol_ = 0;
    frameActive_ = false;
}

void Decoder::startFrame()
{
    frameBuffer_.clear();
    currentByte_ = 0;
    bitsFilled_ = 0;
    expectedPayloadLength_ = 0;
    payloadLengthKnown_ = false;
    pendingSymbol_ = 0;
    frameActive_ = true;
    state_ = State::ReadMark;
}

bool Decoder::matches(long units, long expected) const
{
    return std::llabs(units - expected) <= config_.tolerance(expected);
}

bool Decoder::decodeSymbol(long units, LightLevel level, std::uint8_t& symbolOut) const
{
    if (!matches(units, config_.symbolMarkUnits))
    {
        return false;
    }

    return colorToSymbol(level, symbolOut);
}

void Decoder::abortFrame()
{
    if (frameActive_)
    {
        ++stats_.truncatedFrames;
    }
    reset();
}

void Decoder::finalizeFrame()
{
#if defined(DATAPACKLIB_DEBUG)
    std::cerr << "finalizeFrame: frameSize=" << frameBuffer_.size() << std::endl;
    if (frameBuffer_.size() >= 2)
    {
        std::cerr << " first bytes: " << static_cast<int>(frameBuffer_[0]) << ", "
                  << static_cast<int>(frameBuffer_[1]) << std::endl;
    }
#endif
    if (frameBuffer_.size() < 9)
    {
        ++stats_.headerRejects;
        abortFrame();
        return;
    }

    const std::uint16_t magic = static_cast<std::uint16_t>(frameBuffer_[0]) << 8U | frameBuffer_[1];
    if (magic != config_.magic)
    {
        ++stats_.magicMismatches;
        abortFrame();
        return;
    }

    if (frameBuffer_[2] != config_.version)
    {
        ++stats_.headerRejects;
        abortFrame();
        return;
    }

    const std::size_t payloadLength = (static_cast<std::size_t>(frameBuffer_[3]) << 8U) | frameBuffer_[4];
    if (payloadLength > config_.maxPayloadBytes)
    {
        ++stats_.lengthViolations;
        abortFrame();
        return;
    }

    const std::uint16_t expectedCrc = static_cast<std::uint16_t>(frameBuffer_[5]) << 8U | frameBuffer_[6];

    if (frameBuffer_.size() != 9 + payloadLength)
    {
        ++stats_.truncatedFrames;
        abortFrame();
        return;
    }

    const std::uint16_t ender = static_cast<std::uint16_t>(frameBuffer_[frameBuffer_.size() - 2]) << 8U |
                                frameBuffer_[frameBuffer_.size() - 1];
    if (ender != config_.ender)
    {
        ++stats_.enderMismatches;
        abortFrame();
        return;
    }

    const std::uint16_t computedCrc = computeCrc16(frameBuffer_.data() + 7, payloadLength);
    if (computedCrc != expectedCrc)
    {
        ++stats_.crcFailures;
        abortFrame();
        return;
    }

    if (callback_)
    {
        std::vector<std::uint8_t> payload(frameBuffer_.begin() + 7, frameBuffer_.begin() + 7 + payloadLength);
        callback_(payload);
    }
    ++stats_.framesDecoded;
    reset();
}

void Decoder::handleSymbol(std::uint8_t symbol)
{
#if defined(DATAPACKLIB_DEBUG)
    std::cerr << "handleSymbol value=" << static_cast<int>(symbol) << std::endl;
#endif
    currentByte_ = static_cast<std::uint8_t>((currentByte_ << 2U) | (symbol & 0x03U));
    bitsFilled_ += 2;
    if (bitsFilled_ == 8)
    {
#if defined(DATAPACKLIB_DEBUG)
        std::cerr << " completed byte=" << static_cast<int>(currentByte_) << std::endl;
#endif
        frameBuffer_.push_back(currentByte_);
        currentByte_ = 0;
        bitsFilled_ = 0;

        if (frameBuffer_.size() == 5)
        {
            expectedPayloadLength_ = (static_cast<std::size_t>(frameBuffer_[3]) << 8U) | frameBuffer_[4];
            payloadLengthKnown_ = true;
            if (expectedPayloadLength_ > config_.maxPayloadBytes)
            {
                ++stats_.lengthViolations;
                abortFrame();
                return;
            }
        }

        if (payloadLengthKnown_)
        {
            const std::size_t totalBytesNeeded = 9 + expectedPayloadLength_;
            if (frameBuffer_.size() > totalBytesNeeded)
            {
                abortFrame();
                return;
            }
            if (frameBuffer_.size() == totalBytesNeeded)
            {
                finalizeFrame();
            }
        }
    }
}

void Decoder::feed(const SignalChange& change)
{
    if (change.duration <= 0)
    {
        return;
    }

    const double ratio = static_cast<double>(change.duration) / static_cast<double>(config_.unitDurationMicros);
    const long units = static_cast<long>(std::llround(ratio));
    const double error = std::fabs(ratio - static_cast<double>(units));

    const double driftLimit = std::max(config_.allowedDriftFraction, 0.01);

    auto tryArmPreamble = [&]() {
        if (change.level == config_.preambleColor && matches(units, config_.preambleMarkUnits))
        {
            state_ = State::WaitSpace;
        }
    };

    if (units <= 0 || error > driftLimit)
    {
        ++stats_.durationRejections;
        abortFrame();
        tryArmPreamble();
        return;
    }

    switch (state_)
    {
    case State::Idle:
        if (change.level == config_.preambleColor && matches(units, config_.preambleMarkUnits))
        {
            state_ = State::WaitSpace;
        }
        break;
    case State::WaitSpace:
        if (change.level == LightLevel::Off && matches(units, config_.preambleSpaceUnits))
        {
            startFrame();
        }
        else if (change.level == config_.preambleColor && matches(units, config_.preambleMarkUnits))
        {
            state_ = State::WaitSpace;
        }
        else
        {
            abortFrame();
            tryArmPreamble();
        }
        break;
    case State::ReadMark:
    {
        if (change.level == LightLevel::Off)
        {
            ++stats_.markRejections;
            abortFrame();
            tryArmPreamble();
            break;
        }
        std::uint8_t symbol = 0;
        if (!decodeSymbol(units, change.level, symbol))
        {
            ++stats_.markRejections;
            abortFrame();
            tryArmPreamble();
            break;
        }
        pendingSymbol_ = symbol;
        state_ = State::ReadSpace;
        break;
    }
    case State::ReadSpace:
        if (change.level != LightLevel::Off)
        {
            ++stats_.durationRejections;
            abortFrame();
            tryArmPreamble();
            break;
        }
        if (!matches(units, config_.separatorUnits) && units < config_.separatorUnits)
        {
            ++stats_.durationRejections;
            abortFrame();
            tryArmPreamble();
            break;
        }
        handleSymbol(pendingSymbol_);
        if (state_ == State::ReadSpace)
        {
            state_ = State::ReadMark;
        }
        break;
    }
}

} // namespace datapack
#endif

// Implementation moved to datapacklib.h for Arduino compatibility.
