#include <unity.h>

#include <ArduinoJson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "irrigation/IrrigationIotProtocol.h"

namespace {

std::string readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return input ? contents.str() : std::string();
}

int hexNibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool decodeHex(const char* text, std::vector<uint8_t>& bytes) {
    bytes.clear();
    if (!text) return false;
    const std::size_t length = std::strlen(text);
    if ((length & 1U) != 0U) return false;
    bytes.reserve(length / 2U);
    for (std::size_t index = 0; index < length; index += 2U) {
        const int high = hexNibble(text[index]);
        const int low = hexNibble(text[index + 1U]);
        if (high < 0 || low < 0) return false;
        bytes.push_back(static_cast<uint8_t>((high << 4U) | low));
    }
    return true;
}

void test_shared_platform_command_vectors() {
    const char* labRoot = std::getenv("IOT_DEVICE_LAB_DIR");
    TEST_ASSERT_NOT_NULL_MESSAGE(
        labRoot,
        "set IOT_DEVICE_LAB_DIR to the current iot-device-lab checkout");
    const std::string path =
        std::string(labRoot) +
        "/device-types/irrigation-controller/test/fixtures/platform-command-vectors.json";
    const std::string fixture = readFile(path);
    TEST_ASSERT_FALSE_MESSAGE(fixture.empty(), path.c_str());

    JsonDocument document;
    TEST_ASSERT_FALSE(deserializeJson(document, fixture));
    TEST_ASSERT_EQUAL_STRING("1", document["schemaVersion"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("irrigation-controller",
                             document["typeKey"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("irrigation-controller-6-zone",
                             document["modelKey"].as<const char*>());

    const char* deviceId = document["deviceId"].as<const char*>();
    char expectedTopic[192]{};
    std::snprintf(expectedTopic,
                  sizeof(expectedTopic),
                  "iot/irrigation-controller/v1/%s/command",
                  deviceId);

    const JsonArrayConst cases = document["cases"].as<JsonArrayConst>();
    TEST_ASSERT_GREATER_THAN_UINT32(0, cases.size());
    for (JsonObjectConst testCase : cases) {
        std::string payloadText;
        std::vector<uint8_t> payloadBytes;
        if (!testCase["payloadHex"].isUnbound()) {
            TEST_ASSERT_TRUE_MESSAGE(
                decodeHex(testCase["payloadHex"].as<const char*>(), payloadBytes),
                testCase["id"].as<const char*>());
        } else {
            serializeJson(testCase["payload"], payloadText);
            payloadBytes.assign(payloadText.begin(), payloadText.end());
        }

        IrrigationIotProtocol::CommandPacket packet;
        packet.topic = testCase["topic"].as<const char*>();
        packet.payload = payloadBytes.data();
        packet.payloadLength = payloadBytes.size();
        packet.qos = testCase["qos"].as<uint8_t>();
        packet.retain = testCase["retain"].as<bool>();
        IrrigationIotProtocol::Command command;
        const IrrigationIotProtocol::ParseError result =
            IrrigationIotProtocol::parseCommand(packet, expectedTopic, command);
        const JsonObjectConst expected = testCase["expected"].as<JsonObjectConst>();
        const bool accepted = expected["accepted"].as<bool>();
        if (accepted) {
            TEST_ASSERT_EQUAL_MESSAGE(
                IrrigationIotProtocol::ParseError::None,
                result,
                testCase["id"].as<const char*>());
        } else {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                expected["error"].as<const char*>(),
                IrrigationIotProtocol::parseErrorName(result),
                testCase["id"].as<const char*>());
        }
    }
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_shared_platform_command_vectors);
    return UNITY_END();
}
