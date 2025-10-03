/**
 * @file datapacklib.h
 * @brief Public interface for encoding and decoding IR signal transitions into robust binary frames.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>

#ifndef DATAPACKLIB_MAX_PAYLOAD
#define DATAPACKLIB_MAX_PAYLOAD 512
#endif

#ifndef DATAPACKLIB_MAX_SIGNAL_CHANGES
#define DATAPACKLIB_MAX_SIGNAL_CHANGES ((DATAPACKLIB_MAX_PAYLOAD + 9) * 8 + 32)
#endif

namespace datapack
{

template <typename T, size_t Capacity>
class StaticVector
{
public:
    StaticVector() : size_(0)
    {
    }

    void clear()
    {
        size_ = 0;
    }

    size_t size() const
    {
        return size_;
    }

    size_t capacity() const
    {
        return Capacity;
    }

    bool push_back(const T& value)
    {
        if (size_ >= Capacity)
        {
            return false;
        }
        data_[size_++] = value;
        return true;
    }

    bool append(const T* data, size_t count)
    {
        if (size_ + count > Capacity)
        {
            return false;
        }
        for (size_t i = 0; i < count; ++i)
        {
            data_[size_ + i] = data[i];
        }
        size_ += count;
        return true;
    }

    T& operator[](size_t index)
    {
        return data_[index];
    }

    const T& operator[](size_t index) const
    {
        return data_[index];
    }

    T* data()
    {
        return data_;
    }

    const T* data() const
    {
        return data_;
    }

private:
    T data_[Capacity];
    size_t size_;
};

enum class LightLevel
{
    Off = 0,
    White,
    Red,
    Green,
    Blue
};

struct SignalChange
{
    LightLevel level;
    long duration;
};

namespace detail
{

inline long maxLong(long a, long b)
{
    return (a > b) ? a : b;
}

inline double maxDouble(double a, double b)
{
    return (a > b) ? a : b;
}

inline LightLevel symbolToColor(uint8_t symbol)
{
    switch (symbol & 0x03u)
    {
    case 0:
        return LightLevel::Red;
    case 1:
        return LightLevel::Green;
    case 2:
        return LightLevel::Blue;
    default:
        return LightLevel::White;
    }
}

inline bool colorToSymbol(LightLevel level, uint8_t& symbol)
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

inline uint16_t computeCrc16(const uint8_t* data, size_t size)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit)
        {
            if ((crc & 0x8000u) != 0)
            {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

} // namespace detail

struct ProtocolConfig
{
    long unitDurationMicros = 600;
    long preambleMarkUnits = 16;
    long preambleSpaceUnits = 8;
    long symbolMarkUnits = 1;
    long separatorUnits = 1;
    long frameGapUnits = 12;
    LightLevel preambleColor = LightLevel::White;
    double allowedDriftFraction = 0.20;
    size_t maxPayloadBytes = DATAPACKLIB_MAX_PAYLOAD;
    uint16_t magic = 0xC39A;
    uint16_t ender = 0x51AA;
    uint8_t version = 1;

    long tolerance(long expectedUnits) const
    {
        double fraction = allowedDriftFraction;
        if (fraction < 0.01)
        {
            fraction = 0.01;
        }
        long tol = static_cast<long>(static_cast<double>(expectedUnits) * fraction + 0.5);
        if (tol < 1)
        {
            tol = 1;
        }
        return tol;
    }
};

struct DecoderStats
{
    size_t framesDecoded = 0;
    size_t magicMismatches = 0;
    size_t headerRejects = 0;
    size_t lengthViolations = 0;
    size_t crcFailures = 0;
    size_t enderMismatches = 0;
    size_t durationRejections = 0;
    size_t markRejections = 0;
    size_t truncatedFrames = 0;
};

using SignalBuffer = StaticVector<SignalChange, DATAPACKLIB_MAX_SIGNAL_CHANGES>;

class Encoder
{
public:
    explicit Encoder(const ProtocolConfig& config = {})
    {
        configure(config);
    }

    const ProtocolConfig& config() const noexcept
    {
        return config_;
    }

    bool configure(const ProtocolConfig& config)
    {
        config_ = config;
        valid_ = validateConfig(config_);
        return valid_;
    }

    bool isValid() const noexcept
    {
        return valid_;
    }

    bool encode(const uint8_t* payload, size_t length, SignalBuffer& out) const
    {
        if (!valid_)
        {
            return false;
        }
        if ((payload == NULL && length > 0) || length > config_.maxPayloadBytes || length > DATAPACKLIB_MAX_PAYLOAD)
        {
            return false;
        }

        StaticVector<uint8_t, DATAPACKLIB_MAX_PAYLOAD + 9> frame;
        out.clear();

        if (!frame.push_back(static_cast<uint8_t>(config_.magic >> 8u)))
        {
            return false;
        }
        if (!frame.push_back(static_cast<uint8_t>(config_.magic & 0xFFu)))
        {
            return false;
        }
        if (!frame.push_back(config_.version))
        {
            return false;
        }

        const uint16_t lengthField = static_cast<uint16_t>(length);
        if (!frame.push_back(static_cast<uint8_t>(lengthField >> 8u)))
        {
            return false;
        }
        if (!frame.push_back(static_cast<uint8_t>(lengthField & 0xFFu)))
        {
            return false;
        }

        const uint16_t crc = detail::computeCrc16(payload, length);
        if (!frame.push_back(static_cast<uint8_t>(crc >> 8u)))
        {
            return false;
        }
        if (!frame.push_back(static_cast<uint8_t>(crc & 0xFFu)))
        {
            return false;
        }

        if (!frame.append(payload, length))
        {
            return false;
        }

        if (!frame.push_back(static_cast<uint8_t>(config_.ender >> 8u)))
        {
            return false;
        }
        if (!frame.push_back(static_cast<uint8_t>(config_.ender & 0xFFu)))
        {
            return false;
        }

        bool ok = true;
        struct Writer
        {
            long unitDuration;
            SignalBuffer& buffer;
            bool* status;

            void emit(LightLevel level, long units)
            {
                if (!*status || units <= 0)
                {
                    return;
                }
                SignalChange change;
                change.level = level;
                change.duration = units * unitDuration;
                if (!buffer.push_back(change))
                {
                    *status = false;
                }
            }
        };

        Writer writer = {config_.unitDurationMicros, out, &ok};

        writer.emit(config_.preambleColor, config_.preambleMarkUnits);
        writer.emit(LightLevel::Off, config_.preambleSpaceUnits);

        for (size_t i = 0; i < frame.size(); ++i)
        {
            const uint8_t byte = frame[i];
            for (int shift = 6; shift >= 0; shift -= 2)
            {
                const uint8_t symbol = static_cast<uint8_t>((byte >> shift) & 0x03u);
                writer.emit(detail::symbolToColor(symbol), config_.symbolMarkUnits);
                writer.emit(LightLevel::Off, config_.separatorUnits);
            }
        }

        writer.emit(LightLevel::Off, config_.frameGapUnits);

        if (!ok)
        {
            out.clear();
            return false;
        }

        return true;
    }

private:
    bool validateConfig(const ProtocolConfig& config) const
    {
        if (config.unitDurationMicros <= 0)
        {
            return false;
        }
        if (config.symbolMarkUnits <= 0 || config.separatorUnits <= 0)
        {
            return false;
        }
        if (config.preambleMarkUnits <= 0 || config.preambleSpaceUnits <= 0)
        {
            return false;
        }
        if (config.maxPayloadBytes == 0 || config.maxPayloadBytes > DATAPACKLIB_MAX_PAYLOAD)
        {
            return false;
        }
        return true;
    }

    ProtocolConfig config_{};
    bool valid_ = false;
};

class Decoder
{
public:
    using DataCallback = void (*)(const uint8_t* data, size_t length, void* context);

    Decoder(DataCallback callback = nullptr, void* context = nullptr, const ProtocolConfig& config = {})
        : callback_(callback), callbackContext_(context)
    {
        configure(config);
    }

    void feed(const SignalChange& change)
    {
        if (!valid_ || change.duration <= 0)
        {
            return;
        }

        const double ratio = static_cast<double>(change.duration) /
                              static_cast<double>(config_.unitDurationMicros);
        long units = static_cast<long>(ratio + 0.5);
        const double error = fabs(ratio - static_cast<double>(units));
        const double driftLimit = detail::maxDouble(config_.allowedDriftFraction, 0.01);
        const LightLevel level = change.level;
        const bool invalidTiming = (units <= 0) || (error > driftLimit);
        const long preambleUnits = config_.preambleMarkUnits;

        if (invalidTiming)
        {
            ++stats_.durationRejections;
            abortFrame();
            if (level == config_.preambleColor && matches(units, preambleUnits))
            {
                state_ = State::WaitSpace;
            }
            return;
        }

        switch (state_)
        {
        case State::Idle:
            if (level == config_.preambleColor && matches(units, preambleUnits))
            {
                state_ = State::WaitSpace;
            }
            break;
        case State::WaitSpace:
            if (level == LightLevel::Off && matches(units, config_.preambleSpaceUnits))
            {
                startFrame();
            }
            else if (level == config_.preambleColor && matches(units, preambleUnits))
            {
                state_ = State::WaitSpace;
            }
            else
            {
                abortFrame();
                if (level == config_.preambleColor && matches(units, preambleUnits))
                {
                    state_ = State::WaitSpace;
                }
            }
            break;
        case State::ReadMark:
        {
            if (level == LightLevel::Off)
            {
                ++stats_.markRejections;
                abortFrame();
                if (level == config_.preambleColor && matches(units, preambleUnits))
                {
                    state_ = State::WaitSpace;
                }
                break;
            }
            uint8_t symbol = 0;
            if (!decodeSymbol(units, level, symbol))
            {
                ++stats_.markRejections;
                abortFrame();
                if (level == config_.preambleColor && matches(units, preambleUnits))
                {
                    state_ = State::WaitSpace;
                }
                break;
            }
            pendingSymbol_ = symbol;
            state_ = State::ReadSpace;
            break;
        }
        case State::ReadSpace:
            if (level != LightLevel::Off)
            {
                ++stats_.durationRejections;
                abortFrame();
                if (level == config_.preambleColor && matches(units, preambleUnits))
                {
                    state_ = State::WaitSpace;
                }
                break;
            }
            if (!matches(units, config_.separatorUnits) && units < config_.separatorUnits)
            {
                ++stats_.durationRejections;
                abortFrame();
                if (level == config_.preambleColor && matches(units, preambleUnits))
                {
                    state_ = State::WaitSpace;
                }
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

    void reset()
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

    void setCallback(DataCallback callback, void* context = nullptr)
    {
        callback_ = callback;
        callbackContext_ = context;
    }

    bool configure(const ProtocolConfig& config)
    {
        config_ = config;
        valid_ = validateConfig(config_);
        reset();
        return valid_;
    }

    bool isValid() const noexcept
    {
        return valid_;
    }

    const ProtocolConfig& config() const noexcept
    {
        return config_;
    }

    const DecoderStats& stats() const noexcept
    {
        return stats_;
    }

private:
    enum class State
    {
        Idle,
        WaitSpace,
        ReadMark,
        ReadSpace
    };

    void startFrame()
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

    void handleSymbol(uint8_t symbol)
    {
        currentByte_ = static_cast<uint8_t>((currentByte_ << 2u) | (symbol & 0x03u));
        bitsFilled_ += 2;
        if (bitsFilled_ == 8)
        {
            if (!frameBuffer_.push_back(currentByte_))
            {
                abortFrame();
                return;
            }
            currentByte_ = 0;
            bitsFilled_ = 0;

            const size_t frameSize = frameBuffer_.size();
            if (frameSize == 5)
            {
                expectedPayloadLength_ = static_cast<size_t>(frameBuffer_[3]) << 8 | frameBuffer_[4];
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
                const size_t totalBytesNeeded = 9 + expectedPayloadLength_;
                if (frameSize > totalBytesNeeded)
                {
                    abortFrame();
                    return;
                }
                if (frameSize == totalBytesNeeded)
                {
                    finalizeFrame();
                }
            }
        }
    }

    void finalizeFrame()
    {
        const size_t frameSize = frameBuffer_.size();
        if (frameSize < 9)
        {
            ++stats_.headerRejects;
            abortFrame();
            return;
        }

        const uint16_t magic = static_cast<uint16_t>(frameBuffer_[0]) << 8 | frameBuffer_[1];
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

        const size_t payloadLength = static_cast<size_t>(frameBuffer_[3]) << 8 | frameBuffer_[4];
        if (payloadLength > config_.maxPayloadBytes)
        {
            ++stats_.lengthViolations;
            abortFrame();
            return;
        }

        const uint16_t expectedCrc = static_cast<uint16_t>(frameBuffer_[5]) << 8 | frameBuffer_[6];

        if (frameSize != 9 + payloadLength)
        {
            ++stats_.truncatedFrames;
            abortFrame();
            return;
        }

        const uint16_t ender = static_cast<uint16_t>(frameBuffer_[frameSize - 2]) << 8 |
                                frameBuffer_[frameSize - 1];
        if (ender != config_.ender)
        {
            ++stats_.enderMismatches;
            abortFrame();
            return;
        }

        const uint16_t computedCrc = detail::computeCrc16(frameBuffer_.data() + 7, payloadLength);
        if (computedCrc != expectedCrc)
        {
            ++stats_.crcFailures;
            abortFrame();
            return;
        }

        if (callback_)
        {
            callback_(frameBuffer_.data() + 7, payloadLength, callbackContext_);
        }
        ++stats_.framesDecoded;
        reset();
    }

    void abortFrame()
    {
        if (frameActive_)
        {
            ++stats_.truncatedFrames;
        }
        reset();
    }

    bool matches(long units, long expected) const
    {
        const long diff = labs(units - expected);
        return diff <= config_.tolerance(expected);
    }

    bool decodeSymbol(long units, LightLevel level, uint8_t& symbolOut) const
    {
        if (!matches(units, config_.symbolMarkUnits))
        {
            return false;
        }
        return detail::colorToSymbol(level, symbolOut);
    }

    bool validateConfig(const ProtocolConfig& config) const
    {
        if (config.unitDurationMicros <= 0)
        {
            return false;
        }
        if (config.symbolMarkUnits <= 0 || config.separatorUnits <= 0)
        {
            return false;
        }
        if (config.preambleMarkUnits <= 0 || config.preambleSpaceUnits <= 0)
        {
            return false;
        }
        if (config.maxPayloadBytes == 0 || config.maxPayloadBytes > DATAPACKLIB_MAX_PAYLOAD)
        {
            return false;
        }
        return true;
    }

    ProtocolConfig config_{};
    DataCallback callback_{};
    void* callbackContext_ = nullptr;
    DecoderStats stats_{};
    State state_ = State::Idle;
    StaticVector<uint8_t, DATAPACKLIB_MAX_PAYLOAD + 9> frameBuffer_{};
    uint8_t currentByte_ = 0;
    size_t bitsFilled_ = 0;
    size_t expectedPayloadLength_ = 0;
    bool payloadLengthKnown_ = false;
    uint8_t pendingSymbol_ = 0;
    bool frameActive_ = false;
    bool valid_ = false;
};

} // namespace datapack

