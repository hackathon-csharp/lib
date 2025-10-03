#include "datapacklib.h"

#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

void printData(const std::uint8_t* data, std::size_t length)
{
    std::cout << "Frame decoded successfully!\n";
    std::cout << "Decoded payload (hex):";
    for (std::size_t i = 0; i < length; ++i)
    {
        std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(data[i]);
    }
    std::cout << '\n';
    std::cout << std::dec;

    std::string utf8(reinterpret_cast<const char*>(data), length);
    std::cout << "Decoded payload (utf-8): " << utf8 << '\n';
}

const char* levelToString(datapack::LightLevel level)
{
    using datapack::LightLevel;
    switch (level)
    {
    case LightLevel::Off:
        return "Off";
    case LightLevel::White:
        return "White";
    case LightLevel::Red:
        return "Red";
    case LightLevel::Green:
        return "Green";
    case LightLevel::Blue:
        return "Blue";
    }
    return "Unknown";
}

void printEncodedBuffer(const datapack::SignalBuffer& buffer)
{
    std::cout << "Encoded signal buffer (" << buffer.size() << " changes):\n";
    for (std::size_t i = 0; i < buffer.size(); ++i)
    {
        const datapack::SignalChange& change = buffer[i];
        std::cout << "  [" << i << "] level=" << levelToString(change.level)
                  << ", duration=" << change.duration << " us\n";
    }
}

void decoderCallback(const std::uint8_t* data, std::size_t length, void* /*context*/)
{
    printData(data, length);
}

int main()
{
    using namespace datapack;

    ProtocolConfig config;
    Encoder encoder(config);

    const std::string message = "Привет, мир!";
    const std::vector<std::uint8_t> payload(message.begin(), message.end());

    SignalBuffer encoded;
    if (!encoder.encode(payload.data(), payload.size(), encoded))
    {
        std::cerr << "Failed to encode payload.\n";
        return 1;
    }

    printEncodedBuffer(encoded);

    Decoder decoder(decoderCallback, nullptr, config);

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> noiseCountDist(100, 1000);
    std::uniform_int_distribution<long> durationDist(1, config.unitDurationMicros * config.frameGapUnits * 2);
    std::uniform_int_distribution<int> levelDist(0, 4);

    auto injectNoise = [&](const char *position)
    {
        const int noiseCount = noiseCountDist(rng);
        std::cout << "Injecting " << noiseCount << " noise signal changes " << position
                  << " the real frame.\n";
        for (int i = 0; i < noiseCount; ++i)
        {
            const int levelIndex = levelDist(rng);
            LightLevel level = LightLevel::Off;
            switch (levelIndex)
            {
            case 0:
                level = LightLevel::Off;
                break;
            case 1:
                level = LightLevel::White;
                break;
            case 2:
                level = LightLevel::Red;
                break;
            case 3:
                level = LightLevel::Green;
                break;
            case 4:
                level = LightLevel::Blue;
                break;
            }
            SignalChange noise{level, durationDist(rng)};
            decoder.feed(noise);
        }
    };

    injectNoise("before");

    for (std::size_t i = 0; i < encoded.size(); ++i)
    {
        decoder.feed(encoded[i]);
    }

    injectNoise("after");

    for (std::size_t i = 0; i < encoded.size(); ++i)
    {
        decoder.feed(encoded[i]);
    }

    const DecoderStats stats = decoder.stats();
    std::cout << std::endl;
    std::cout << "Frames decoded: " << stats.framesDecoded << '\n';
    std::cout << "Magic mismatches: " << stats.magicMismatches << '\n';
    std::cout << "Header rejects: " << stats.headerRejects << '\n';
    std::cout << "Length violations: " << stats.lengthViolations << '\n';
    std::cout << "CRC failures: " << stats.crcFailures << '\n';
    std::cout << "Ender mismatches: " << stats.enderMismatches << '\n';
    std::cout << "Duration rejections: " << stats.durationRejections << '\n';
    std::cout << "Mark rejections: " << stats.markRejections << '\n';
    std::cout << "Truncated frames: " << stats.truncatedFrames << '\n';

    return 0;
}
