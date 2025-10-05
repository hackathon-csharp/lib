/**
 * @file datapacklib.h
 * @brief Public interface for encoding and decoding multi-colour IR signal transitions.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

/*
    Структура пакета данных:
    1 байт - индекс слова в буффере
    2 байта - слово
    1 байт - контрольная сумма
*/

namespace datapack
{
    int min_duration = 60; // порог игнора
    int duration = 125;    // продолжительность каждого сигнала

    template <typename T, std::size_t Capacity>
    class StaticVector
    {
    public:
        StaticVector() noexcept : size_(0) {}

        std::size_t size() const noexcept { return size_; }

        constexpr std::size_t capacity() const noexcept { return Capacity; }

        bool push_back(const T &value) noexcept
        {
            if (size_ >= Capacity)
            {
                return false;
            }
            data_[size_++] = value;
            return true;
        }

        bool append(const T *data, std::size_t count) noexcept
        {
            if (size_ + count > Capacity)
            {
                return false;
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                data_[size_ + i] = data[i];
            }
            size_ += count;
            return true;
        }

        void clear() noexcept { size_ = 0; }

        T &operator[](std::size_t index) noexcept { return data_[index]; }

        const T &operator[](std::size_t index) const noexcept { return data_[index]; }

        T *data() noexcept { return data_; }

        const T *data() const noexcept { return data_; }

        void shift_and_push(const T &value) noexcept
        {
            if (size_ > 0)
            {
                for (std::size_t i = 0; i < size_ - 1; ++i)
                {
                    data_[i] = data_[i + 1];
                }
            }
            if (size_ < Capacity)
            {
                data_[size_] = value;
                ++size_;
            }
            else
            {
                data_[Capacity - 1] = value;
            }
        }

    private:
        T data_[Capacity];
        std::size_t size_;
    };

    struct UnpackedPackage
    {
        bool valid;
        uint8_t index;
        uint16_t word;
    };

    enum class LightLevel
    {
        Off = 0,
        White = 1,
        Red = 2,
        Green = 3,
        Blue = 4
    };

    LightLevel levels[5] = {LightLevel::Off, LightLevel::White, LightLevel::Red, LightLevel::Green, LightLevel::Blue};

    struct SignalChange
    {
        LightLevel value;
        long duration;
    };

    StaticVector<SignalChange, 4096> send_commands;
    static StaticVector<uint16_t, 256> send_buffer;
    static uint16_t receive_buffer[256];
    static uint32_t window = 12345678;
    static void (*onPacketReceived)(UnpackedPackage) = nullptr;

    int8_t getDbit(LightLevel prev, LightLevel curr)
    {
        switch (prev)
        {
        case LightLevel::Off:
        {
            switch (curr)
            {
            case LightLevel::Off:
            case LightLevel::White:
                return 0;
            case LightLevel::Red:
                return 1;
            case LightLevel::Green:
                return 2;
            case LightLevel::Blue:
                return 3;
            default:
                return 0;
            }
        }
        case LightLevel::White:
        {
            switch (curr)
            {
            case LightLevel::White:
            case LightLevel::Off:
                return 0;
            case LightLevel::Red:
                return 1;
            case LightLevel::Green:
                return 2;
            case LightLevel::Blue:
                return 3;
            default:
                return 0;
            }
        }
        case LightLevel::Red:
        {
            switch (curr)
            {
            case LightLevel::Red:
            case LightLevel::Off:
                return 0;
            case LightLevel::White:
                return 1;
            case LightLevel::Green:
                return 2;
            case LightLevel::Blue:
                return 3;
            default:
                return 0;
            }
        }
        case LightLevel::Green:
        {
            switch (curr)
            {
            case LightLevel::Green:
            case LightLevel::Off:
                return 0;
            case LightLevel::White:
                return 1;
            case LightLevel::Red:
                return 2;
            case LightLevel::Blue:
                return 3;
            default:
                return 0;
            }
        }
        case LightLevel::Blue:
        {
            switch (curr)
            {
            case LightLevel::Blue:
            case LightLevel::Off:
                return 0;
            case LightLevel::White:
                return 1;
            case LightLevel::Red:
                return 2;
            case LightLevel::Green:
                return 3;
            default:
                return 0;
            }
        }
        }
        return 0;
    }

    LightLevel getLightForDbit(LightLevel prev, uint8_t data)
    {
        if (data > 3)
            data = 3;
        for (LightLevel ll : levels)
        {
            if (ll == prev)
                continue;
            uint8_t expectedData = getDbit(prev, ll);
            if (data == expectedData)
                return ll;
        }
        return LightLevel::Off;
    }

    static uint8_t crs8(uint16_t data, uint8_t index)
    {
        uint8_t bytes[3] = {static_cast<uint8_t>(data & 0xFF), static_cast<uint8_t>((data >> 8) & 0xFF), index};
        uint8_t crc = 0;
        for (int i = 0; i < 3; ++i)
        {
            crc ^= bytes[i];
            for (int j = 0; j < 8; ++j)
            {
                if (crc & 0x80)
                {
                    crc = (crc << 1) ^ 0x07;
                }
                else
                {
                    crc <<= 1;
                }
            }
        }
        return crc;
    }

    static void encode()
    {
        LightLevel prevValue = LightLevel::Off;
        send_commands.clear();
        for (size_t i = 0; i < send_buffer.size(); i++)
        {
            uint8_t index = i;
            uint16_t data = send_buffer[i];
            uint8_t crc = crs8(data, index);

            // Кодируем index (1 байт)
            for (int bitpair = 3; bitpair >= 0; --bitpair)
            {
                uint8_t twobits = (index >> (bitpair * 2)) & 0x03;
                LightLevel next = getLightForDbit(prevValue, twobits);
                send_commands.push_back({next, duration});
                prevValue = next;
            }

            // Кодируем data low byte (1 байт)
            uint8_t data_low = data & 0xFF;
            for (int bitpair = 3; bitpair >= 0; --bitpair)
            {
                uint8_t twobits = (data_low >> (bitpair * 2)) & 0x03;
                LightLevel next = getLightForDbit(prevValue, twobits);
                send_commands.push_back({next, duration});
                prevValue = next;
            }

            // Кодируем data high byte (1 байт)
            uint8_t data_high = (data >> 8) & 0xFF;
            for (int bitpair = 3; bitpair >= 0; --bitpair)
            {
                uint8_t twobits = (data_high >> (bitpair * 2)) & 0x03;
                LightLevel next = getLightForDbit(prevValue, twobits);
                send_commands.push_back({next, duration});
                prevValue = next;
            }

            // Кодируем crc (1 байт)
            for (int bitpair = 3; bitpair >= 0; --bitpair)
            {
                uint8_t twobits = (crc >> (bitpair * 2)) & 0x03;
                LightLevel next = getLightForDbit(prevValue, twobits);
                send_commands.push_back({next, duration});
                prevValue = next;
            }
        }
    }

    void setSendData(const uint8_t *data, size_t len)
    {
        send_buffer.clear();
        if (len > send_buffer.capacity())
            len = send_buffer.capacity();
        for (size_t i = 0; i < len; i += 2)
        {
            uint16_t word = data[i];
            if (i + 1 < len)
            {
                word |= (static_cast<uint16_t>(data[i + 1]) << 8);
            }
            send_buffer.push_back(word);
        }
        encode();
    }

    size_t getReceivedData(uint8_t *data)
    {
        size_t index = 0;
        for (size_t i = 0; i < sizeof(receive_buffer) / sizeof(uint16_t); ++i)
        {
            uint16_t word = receive_buffer[i];
            data[index++] = word & 0xFF;
            data[index++] = (word >> 8) & 0xFF;
        }
        return index;
    }

    LightLevel prev_value = LightLevel::Off;

    static UnpackedPackage unpack_and_check(uint32_t packet)
    {
        // Извлекаем байты из packet
        // Структура: index (1 байт), word_low (1 байт), word_high (1 байт), crc (1 байт)
        uint8_t index = (packet >> 24) & 0xFF;
        uint8_t word_low = (packet >> 16) & 0xFF;
        uint8_t word_high = (packet >> 8) & 0xFF;
        uint8_t crc = packet & 0xFF;
        uint16_t word = (static_cast<uint16_t>(word_high) << 8) | word_low;

        // Вычисляем ожидаемую контрольную сумму
        uint8_t expected_crc = crs8(word, index);

        // Проверяем
        if (crc == expected_crc)
        {
            return {true, index, word};
        }
        else
        {
            return {false, 0, 0};
        }
    }

    void feed(SignalChange sc)
    {
        if (sc.duration < min_duration || sc.value == prev_value)
            return;

        uint8_t data = getDbit(prev_value, sc.value);
        window <<= 2;
        window |= data;
        UnpackedPackage package = unpack_and_check(window);
        if (package.valid)
        {
            receive_buffer[package.index] = package.word;
            if (onPacketReceived)
                onPacketReceived(package);
        }

        prev_value = sc.value;
    }
}