#include "datapacklib.h"

#include <iomanip>
#include <iostream>
#include <vector>
#include <string>

void onPacketReceived(datapack::UnpackedPackage pkg) {
    std::cout << "Received package: valid=" << (pkg.valid ? "true" : "false")
              << ", index=" << static_cast<int>(pkg.index)
              << ", word=0x" << std::hex << pkg.word << std::dec << std::endl;
}

int main() {
    // Устанавливаем callback для полученных пакетов
    datapack::onPacketReceived = onPacketReceived;

    // Подготавливаем данные для отправки: несколько слов
    std::string message = "Hello, IR! Привет мир!";
    datapack::setSendData(reinterpret_cast<const uint8_t *>(message.data()), message.size());

    std::cout << "Send buffer size: " << datapack::send_buffer.size() << std::endl;
    std::cout << "Encoded commands size: " << datapack::send_commands.size() << std::endl;

    // Выводим первые несколько команд для демонстрации
    std::cout << "First 10 encoded commands:" << std::endl;
    for (size_t i = 0; i < 10 && i < datapack::send_commands.size(); ++i) {
        auto cmd = datapack::send_commands[i];
        std::cout << "  Level: " << static_cast<int>(cmd.value) << ", Duration: " << cmd.duration << std::endl;
    }

    // Симулируем прием: подаем все команды обратно в feed
    std::cout << "\nSimulating reception..." << std::endl;
    for (size_t i = 0; i < datapack::send_commands.size(); ++i) {
        datapack::feed(datapack::send_commands[i]);
    }

    // Получаем полученные данные
    uint8_t receivedData[512];
    size_t receivedLen = datapack::getReceivedData(receivedData);

    std::cout << "\nReceived data (" << receivedLen << " bytes):" << std::endl;
    for (size_t i = 0; i < receivedLen; ++i) {
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(receivedData[i]) << " ";
        if ((i + 1) % 16 == 0) std::cout << std::endl;
    }
    std::cout << std::dec << std::endl;

    std::string receivedMessage(reinterpret_cast<char *>(receivedData), receivedLen);
    std::cout << "Received message: " << receivedMessage << std::endl;

    // Проверяем совпадение первых message.size() байт
    bool match = true;
    for (size_t i = 0; i < message.size(); ++i) {
        if (static_cast<uint8_t>(message[i]) != receivedData[i]) {
            match = false;
            break;
        }
    }
    std::cout << "Data integrity check: " << (match ? "PASSED" : "FAILED") << std::endl;

    return 0;
}
