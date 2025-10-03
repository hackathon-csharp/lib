#include "datapacklib.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <utility>

#if defined(DATAPACKLIB_DEBUG)
#include <iostream>
#endif

namespace datapack
{

namespace
{
constexpr std::uint16_t CRC_POLY = 0x1021;
constexpr std::uint16_t CRC_INIT = 0xFFFF;

constexpr LightLevel SYMBOL_TO_COLOR[4] = {
    LightLevel::Red,
    LightLevel::Green,
    LightLevel::Blue,
    LightLevel::White
};

bool colorToSymbol(LightLevel level, std::uint8_t& symbol)
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

std::uint16_t computeCrc16(const std::uint8_t* data, std::size_t size)
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
    explicit SignalWriter(long unitDuration, std::vector<SignalChange>& target)
        : unitDuration_(unitDuration), target_(target)
    {
    }

    void emit(LightLevel level, long units)
    {
        if (units <= 0)
        {
            return;
        }
        const long duration = units * unitDuration_;
        target_.push_back(SignalChange{level, duration});
    }

private:
    long unitDuration_;
    std::vector<SignalChange>& target_;
};

} // namespace

long ProtocolConfig::tolerance(long expectedUnits) const
{
    const auto fraction = std::max(allowedDriftFraction, 0.01);
    const double raw = std::ceil(static_cast<double>(expectedUnits) * fraction);
    const long minTolerance = 1;
    return std::max<long>(minTolerance, static_cast<long>(raw));
}

Encoder::Encoder(ProtocolConfig config)
    : config_(config)
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
}

const ProtocolConfig& Encoder::config() const noexcept
{
    return config_;
}

std::vector<SignalChange> Encoder::encode(const std::vector<std::uint8_t>& payload) const
{
    if (payload.size() > config_.maxPayloadBytes)
    {
        throw std::invalid_argument("payload exceeds maxPayloadBytes");
    }

    std::vector<std::uint8_t> frame;
    frame.reserve(9 + payload.size());
    frame.push_back(static_cast<std::uint8_t>(config_.magic >> 8U));
    frame.push_back(static_cast<std::uint8_t>(config_.magic & 0xFFU));
    frame.push_back(config_.version);

    const std::uint16_t length = static_cast<std::uint16_t>(payload.size());
    frame.push_back(static_cast<std::uint8_t>(length >> 8U));
    frame.push_back(static_cast<std::uint8_t>(length & 0xFFU));

    const std::uint16_t crc = computeCrc16(payload.data(), payload.size());
    frame.push_back(static_cast<std::uint8_t>(crc >> 8U));
    frame.push_back(static_cast<std::uint8_t>(crc & 0xFFU));

    frame.insert(frame.end(), payload.begin(), payload.end());

    frame.push_back(static_cast<std::uint8_t>(config_.ender >> 8U));
    frame.push_back(static_cast<std::uint8_t>(config_.ender & 0xFFU));

    std::vector<SignalChange> result;
    result.reserve(frame.size() * 8 + 8);
    SignalWriter writer(config_.unitDurationMicros, result);

    writer.emit(config_.preambleColor, config_.preambleMarkUnits);
    writer.emit(LightLevel::Off, config_.preambleSpaceUnits);

    const auto writeSymbol = [&](std::uint8_t symbol) {
        const LightLevel level = SYMBOL_TO_COLOR[symbol & 0x03U];
        writer.emit(level, config_.symbolMarkUnits);
        writer.emit(LightLevel::Off, config_.separatorUnits);
    };

    for (std::uint8_t byte : frame)
    {
        for (int shift = 6; shift >= 0; shift -= 2)
        {
            const std::uint8_t symbol = static_cast<std::uint8_t>((byte >> shift) & 0x03U);
            writeSymbol(symbol);
        }
    }

    writer.emit(LightLevel::Off, config_.frameGapUnits);

    return result;
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
