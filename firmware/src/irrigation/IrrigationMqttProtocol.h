#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "IrrigationTypes.h"

class IrrigationMqttProtocol {
public:
    static constexpr uint8_t kVersion = 1;
    static constexpr std::size_t kCommandIdCapacity = 49;

    enum class Action : uint8_t {
        SetPlan,
        DeletePlan,
        PauseAutomatic,
        ResumeAutomatic,
        StartPlan,
        StartManual,
        Stop,
    };

    enum class ParseError : uint8_t {
        None,
        MalformedJson,
        InvalidEnvelope,
        UnsupportedVersion,
        UnsupportedAction,
        InvalidArguments,
    };

    struct Command {
        Action action = Action::Stop;
        std::array<char, kCommandIdCapacity> id{};
        uint32_t revision = 0;
        uint8_t planId = 0;
        bool planEnabled = false;
        std::array<char, kObjectNameCapacity> planName{};
        std::array<uint16_t, kPlanStartTimeCount> startMinutes{};
        std::array<uint16_t, BoardPins::kZoneCount> zoneDurationMinutes{};
        uint32_t resumeAtEpoch = 0;
    };

    static bool parse(const uint8_t* payload,
                      std::size_t payloadLength,
                      Command& command,
                      ParseError& error);
    static const char* parseErrorName(ParseError error);
};
