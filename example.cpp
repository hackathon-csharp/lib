#include "datapacklib.h"

#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

void printData(const std::vector<std::uint8_t>& data)
{
    std::cout << "Frame decoded successfully!\n";
    std::cout << "Decoded payload (hex):";
    for (std::uint8_t byte : data)
    {
        std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(byte);
    }
    std::cout << '\n';
    std::cout << std::dec;

    std::string utf8(data.begin(), data.end());
    std::cout << "Decoded payload (utf-8): " << utf8 << '\n';
}

int main()
{
    using namespace datapack;

    ProtocolConfig config;
    Encoder encoder(config);

    const std::string message = "Привет, мир!";
    const std::vector<std::uint8_t> payload(message.begin(), message.end());

    const std::vector<SignalChange> encoded = encoder.encode(payload);

    Decoder decoder(
        [&](const std::vector<std::uint8_t>& data) {
            printData(data);
        },
        config);

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

    for (const auto &change : encoded)
    {
        decoder.feed(change);
    }

    injectNoise("after");

    for (const auto &change : encoded)
    {
        decoder.feed(change);
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
