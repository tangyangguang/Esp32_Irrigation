#pragma once

#include <Esp32Base.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "IrrigationMqttProtocol.h"
#include "IrrigationTypes.h"

class IrrigationApp;

class IrrigationMqtt {
public:
    static constexpr std::size_t kPayloadCapacity = 1025;

    static IrrigationMqtt& instance();

    bool configure(IrrigationApp& app);
    void handle();
    bool enabled() const;

private:
    static constexpr std::size_t kTopicCapacity = 129;
    static constexpr std::size_t kRecentCommandCount = 8;

    IrrigationMqtt() = default;

    static void messageCallback(const Esp32BaseMqtt::MessageView& message,
                                void* context);
    static void eventCallback(const Esp32BaseMqtt::Event& event,
                              void* context);
    void onMessage(const Esp32BaseMqtt::MessageView& message);
    void onEvent(const Esp32BaseMqtt::Event& event);
    void execute(const IrrigationMqttProtocol::Command& command);
    bool publishAvailability();
    bool publishMeta();
    bool publishState();
    bool publishRun();
    bool publishLatestRecord();
    bool publishPlan(uint8_t planId);
    bool publishResult(const char* commandId,
                       bool success,
                       const char* code);
    bool publishPayload(const char* topic,
                        const char* payload,
                        std::size_t length,
                        bool retain);
    void markAllDirty();
    void observeChanges();
    bool isDuplicateCommand(const char* commandId) const;
    void rememberCommand(const char* commandId);
    uint32_t stateFingerprint() const;

    IrrigationApp* app_ = nullptr;
    bool enabled_ = false;
    bool connected_ = false;
    bool metaDirty_ = false;
    bool stateDirty_ = false;
    bool runDirty_ = false;
    bool latestRecordDirty_ = false;
    bool observedWateringActive_ = false;
    uint8_t plansDirty_ = 0;
    uint8_t nextRecentCommand_ = 0;
    uint32_t observedRevision_ = 0;
    uint32_t observedStateFingerprint_ = 0;
    uint32_t lastStatePublishMs_ = 0;
    uint32_t lastPublishAttemptMs_ = 0;
    std::array<char, kTopicCapacity> rootTopic_{};
    std::array<char, kTopicCapacity> availabilityTopic_{};
    std::array<char, kTopicCapacity> metaTopic_{};
    std::array<char, kTopicCapacity> stateTopic_{};
    std::array<char, kTopicCapacity> runTopic_{};
    std::array<char, kTopicCapacity> latestRecordTopic_{};
    std::array<char, kTopicCapacity> commandTopic_{};
    std::array<char, kTopicCapacity> resultTopic_{};
    std::array<char, 40> deviceId_{};
    std::array<char, 64> clientId_{};
    std::array<std::array<char, IrrigationMqttProtocol::kCommandIdCapacity>,
               kRecentCommandCount>
        recentCommands_{};
    std::array<char, kPayloadCapacity> pendingResult_{};
    std::size_t pendingResultLength_ = 0;
    Esp32BaseMqtt::LastWill lastWill_{};
    IrrigationConfig configScratch_{};
};
