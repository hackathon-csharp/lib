/**
 * @file datapacklib.h
 * @brief Public interface for encoding and decoding IR signal transitions into robust binary frames.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace datapack
{

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
    std::size_t maxPayloadBytes = 512;
    std::uint16_t magic = 0xC39A;
    std::uint16_t ender = 0x51AA;
    std::uint8_t version = 1;

    [[nodiscard]] long tolerance(long expectedUnits) const;
};

struct DecoderStats
{
    std::size_t framesDecoded = 0;
    std::size_t magicMismatches = 0;
    std::size_t headerRejects = 0;
    std::size_t lengthViolations = 0;
    std::size_t crcFailures = 0;
    std::size_t enderMismatches = 0;
    std::size_t durationRejections = 0;
    std::size_t markRejections = 0;
    std::size_t truncatedFrames = 0;
};

class Encoder
{
public:
    explicit Encoder(ProtocolConfig config = {});

    [[nodiscard]] const ProtocolConfig& config() const noexcept;

    [[nodiscard]] std::vector<SignalChange> encode(const std::vector<std::uint8_t>& payload) const;

private:
    ProtocolConfig config_{};
};

class Decoder
{
public:
    using DataCallback = std::function<void(const std::vector<std::uint8_t>&)>;

    Decoder(DataCallback callback, ProtocolConfig config = {});

    void feed(const SignalChange& change);
    void reset();
    void setCallback(DataCallback callback);

    [[nodiscard]] const ProtocolConfig& config() const noexcept;
    [[nodiscard]] const DecoderStats& stats() const noexcept;

private:
    enum class State
    {
        Idle,
        WaitSpace,
        ReadMark,
        ReadSpace
    };

    void startFrame();
    void handleSymbol(std::uint8_t symbol);
    void finalizeFrame();
    void abortFrame();
    [[nodiscard]] bool matches(long units, long expected) const;
    [[nodiscard]] bool decodeSymbol(long units, LightLevel level, std::uint8_t& symbolOut) const;

    ProtocolConfig config_{};
    DataCallback callback_{};
    DecoderStats stats_{};
    State state_ = State::Idle;
    std::vector<std::uint8_t> frameBuffer_{};
    std::uint8_t currentByte_ = 0;
    std::size_t bitsFilled_ = 0;
    std::size_t expectedPayloadLength_ = 0;
    bool payloadLengthKnown_ = false;
    std::uint8_t pendingSymbol_ = 0;
    bool frameActive_ = false;
};

} // namespace datapack

