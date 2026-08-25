#include <unity.h>

#include <array>
#include <cstring>

#include "irrigation/IrrigationMqttCore.h"

namespace {

using namespace IrrigationMqttCore;
using Capability = IrrigationPlatformProtocol::Capability;

constexpr const char* kCommandA = "11111111-1111-4111-8111-111111111111";
constexpr const char* kCommandB = "22222222-2222-4222-8222-222222222222";
constexpr const char* kStopCommand = "33333333-3333-4333-8333-333333333333";

std::array<uint8_t, 32> signature(uint8_t marker) {
    std::array<uint8_t, 32> value{};
    value.fill(marker);
    return value;
}

EvidenceEntry& admitted(EvidenceStore& store,
                        const char* commandId,
                        uint8_t marker,
                        Capability capability = Capability::StartManual) {
    const auto result = store.admit(commandId, signature(marker), 2000, capability, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(Admission::New),
                      static_cast<int>(result.result));
    TEST_ASSERT_NOT_NULL(result.entry);
    return *result.entry;
}

void test_duplicate_replays_latest_evidence_without_second_action() {
    EvidenceStore store;
    uint8_t businessActions = 0;
    EvidenceStatus emitted[3]{};
    uint8_t emittedCount = 0;

    EvidenceEntry& entry = admitted(store, kCommandA, 0x11);
    ++businessActions;
    TEST_ASSERT_TRUE(store.markAccepted(entry));
    emitted[emittedCount++] = entry.receipt;
    TEST_ASSERT_TRUE(store.markRunning(entry));
    emitted[emittedCount++] = entry.progress;
    TEST_ASSERT_TRUE(store.markTerminal(entry, EvidenceStatus::Succeeded));
    emitted[emittedCount++] = entry.progress;

    const auto duplicate = store.admit(kCommandA, signature(0x11), 2000,
                                       Capability::StartManual, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(Admission::Replay),
                      static_cast<int>(duplicate.result));
    TEST_ASSERT_EQUAL_UINT8(1, businessActions);
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Accepted),
                      static_cast<int>(duplicate.entry->receipt));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Succeeded),
                      static_cast<int>(duplicate.entry->progress));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Accepted),
                      static_cast<int>(emitted[0]));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Running),
                      static_cast<int>(emitted[1]));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Succeeded),
                      static_cast<int>(emitted[2]));
}

void test_conflicting_signature_is_silent_and_keeps_original_evidence() {
    EvidenceStore store;
    EvidenceEntry& entry = admitted(store, kCommandA, 0x21);
    TEST_ASSERT_TRUE(store.markAccepted(entry));
    TEST_ASSERT_TRUE(store.markRunning(entry));

    const auto conflict = store.admit(kCommandA, signature(0x22), 2000,
                                      Capability::StartManual, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(Admission::Conflict),
                      static_cast<int>(conflict.result));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Accepted),
                      static_cast<int>(entry.receipt));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Running),
                      static_cast<int>(entry.progress));
    TEST_ASSERT_EQUAL_UINT8(0x21, entry.signature[0]);
}

void test_stop_finishes_original_before_stop_and_idle_stop_succeeds() {
    EvidenceStore store;
    EvidenceEntry& active = admitted(store, kCommandA, 0x31);
    TEST_ASSERT_TRUE(store.markAccepted(active));
    TEST_ASSERT_TRUE(store.markRunning(active));
    EvidenceEntry& stop = admitted(store, kStopCommand, 0x32, Capability::Stop);
    TEST_ASSERT_TRUE(store.markAccepted(stop));
    TEST_ASSERT_TRUE(store.markRunning(stop));

    RemoteOperationTracker tracker;
    tracker.start(kCommandA);
    tracker.requestStop(kStopCommand);
    const StopCompletion completion = tracker.completeStoppedOperation();
    TEST_ASSERT_EQUAL_UINT8(2, completion.count);
    TEST_ASSERT_EQUAL_STRING(kCommandA, completion.updates[0].commandId.data());
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Failed),
                      static_cast<int>(completion.updates[0].status));
    TEST_ASSERT_EQUAL_STRING("stopped", completion.updates[0].reason);
    TEST_ASSERT_EQUAL_STRING(kStopCommand, completion.updates[1].commandId.data());
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Succeeded),
                      static_cast<int>(completion.updates[1].status));
    TEST_ASSERT_TRUE(store.markTerminal(active, completion.updates[0].status,
                                        completion.updates[0].reason));
    TEST_ASSERT_TRUE(store.markTerminal(stop, completion.updates[1].status));

    EvidenceEntry& idleStop = admitted(store, kCommandB, 0x33, Capability::Stop);
    TEST_ASSERT_TRUE(store.markAccepted(idleStop));
    TEST_ASSERT_TRUE(store.markRunning(idleStop));
    tracker.requestStop(kCommandB);
    const StopCompletion idle = tracker.completeIdleStop();
    TEST_ASSERT_EQUAL_UINT8(1, idle.count);
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Succeeded),
                      static_cast<int>(idle.updates[0].status));
    TEST_ASSERT_TRUE(store.markTerminal(idleStop, idle.updates[0].status));
}

void test_restart_preserves_receipt_but_removes_untrusted_running_claim() {
    EvidenceStore store;
    EvidenceEntry& running = admitted(store, kCommandA, 0x41);
    TEST_ASSERT_TRUE(store.markAccepted(running));
    TEST_ASSERT_TRUE(store.markRunning(running));
    EvidenceEntry& terminal = admitted(store, kCommandB, 0x42);
    TEST_ASSERT_TRUE(store.markAccepted(terminal));
    TEST_ASSERT_TRUE(store.markRunning(terminal));
    TEST_ASSERT_TRUE(store.markTerminal(terminal, EvidenceStatus::Succeeded));

    TEST_ASSERT_TRUE(store.validate());
    TEST_ASSERT_TRUE(store.reconcileAfterRestart());
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Accepted),
                      static_cast<int>(running.receipt));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::None),
                      static_cast<int>(running.progress));
    TEST_ASSERT_FALSE(shouldReplayProgress(running.progress));
    TEST_ASSERT_TRUE(shouldReplayProgress(terminal.progress));
    TEST_ASSERT_FALSE(store.markTerminal(running, EvidenceStatus::Failed,
                                         "device_restarted"));
}

void test_connect_decision_replays_full_state_and_only_known_progress() {
    constexpr ConnectionDecision decision = connectedDecision();
    TEST_ASSERT_TRUE(decision.publishOnlineAvailability);
    TEST_ASSERT_TRUE(decision.publishFullState);
    TEST_ASSERT_TRUE(decision.replayKnownProgress);
    TEST_ASSERT_TRUE(shouldReplayProgress(EvidenceStatus::Running));
    TEST_ASSERT_TRUE(shouldReplayProgress(EvidenceStatus::Succeeded));
    TEST_ASSERT_TRUE(shouldReplayProgress(EvidenceStatus::Failed));
    TEST_ASSERT_FALSE(shouldReplayProgress(EvidenceStatus::None));
}

void test_transport_policy_and_stop_slot_prevent_stop_starvation() {
    using Queue = CommandQueue<512>;
    Queue queue;
    const char* normal =
        R"JSON({"capabilityKey":"operation.start-manual","parameters":{}})JSON";
    const char* another =
        R"JSON({"capabilityKey":"parameter.plans","parameters":{}})JSON";
    const char* stop =
        R"JSON({ "parameters": {}, "capability\u004bey" : "operation.stop" })JSON";

    TEST_ASSERT_TRUE(acceptsCommandTransport(1, false));
    TEST_ASSERT_FALSE(acceptsCommandTransport(0, false));
    TEST_ASSERT_FALSE(acceptsCommandTransport(1, true));
    TEST_ASSERT_EQUAL(static_cast<int>(QueuePushResult::AcceptedNormal),
                      static_cast<int>(queue.push(normal, std::strlen(normal), 1, false)));
    TEST_ASSERT_EQUAL(static_cast<int>(QueuePushResult::NormalSlotFull),
                      static_cast<int>(queue.push(another, std::strlen(another), 1, false)));
    TEST_ASSERT_EQUAL(static_cast<int>(QueuePushResult::AcceptedStop),
                      static_cast<int>(queue.push(stop, std::strlen(stop), 1, false)));
    TEST_ASSERT_EQUAL(static_cast<int>(QueuePushResult::StopSlotFull),
                      static_cast<int>(queue.push(stop, std::strlen(stop), 1, false)));
    TEST_ASSERT_EQUAL(static_cast<int>(QueuePushResult::InvalidTransport),
                      static_cast<int>(queue.push(stop, std::strlen(stop), 1, true)));

    Queue::Packet packet;
    TEST_ASSERT_TRUE(queue.pop(packet));
    TEST_ASSERT_TRUE(isStopCommandEnvelope(packet.payload.data(), packet.length));
    TEST_ASSERT_TRUE(queue.pop(packet));
    TEST_ASSERT_EQUAL_STRING(normal, packet.payload.data());
    TEST_ASSERT_FALSE(queue.pop(packet));
}

void test_event_cursor_advances_only_after_matching_persisted_puback() {
    EventCursor cursor;
    cursor.prepare(3, 5, 9, 5);  // Stored cursor was rotated out.
    TEST_ASSERT_EQUAL_UINT32(5, cursor.nextRecordId());
    cursor.enqueued(41, 5);
    TEST_ASSERT_TRUE(cursor.awaitingAck());
    TEST_ASSERT_EQUAL_UINT32(0, cursor.matchingAckRecord(40));
    TEST_ASSERT_FALSE(cursor.commitAck(40, true));
    TEST_ASSERT_FALSE(cursor.commitAck(41, false));
    TEST_ASSERT_EQUAL_UINT32(5, cursor.nextRecordId());
    TEST_ASSERT_TRUE(cursor.awaitingAck());
    TEST_ASSERT_TRUE(cursor.commitAck(41, true));
    TEST_ASSERT_EQUAL_UINT32(6, cursor.nextRecordId());

    cursor.enqueued(42, 6);
    cursor.disconnected();
    TEST_ASSERT_FALSE(cursor.awaitingAck());
    TEST_ASSERT_EQUAL_UINT32(6, cursor.nextRecordId());
    TEST_ASSERT_FALSE(cursor.commitAck(42, true));
    TEST_ASSERT_FALSE(cursor.skipRecord(6, 9, false));
    TEST_ASSERT_EQUAL_UINT32(6, cursor.nextRecordId());
    TEST_ASSERT_TRUE(cursor.skipRecord(6, 9, true));
    TEST_ASSERT_EQUAL_UINT32(7, cursor.nextRecordId());
    TEST_ASSERT_FALSE(cursor.skipRecord(10, 9, true));
    TEST_ASSERT_EQUAL_UINT32(0, cursor.nextRecordId());
}

void test_fixed_channel_qos_and_retain_policy_matches_adapter_contract() {
    for (uint8_t raw = static_cast<uint8_t>(Channel::Availability);
         raw <= static_cast<uint8_t>(Channel::Progress); ++raw) {
        const Channel channel = static_cast<Channel>(raw);
        const PublicationPolicy policy = publicationPolicy(channel);
        TEST_ASSERT_EQUAL_UINT8(1, policy.qos);
        TEST_ASSERT_EQUAL(channel == Channel::Availability, policy.retain);
    }
    TEST_ASSERT_EQUAL_STRING("availability", channelName(Channel::Availability));
    TEST_ASSERT_EQUAL_STRING("state", channelName(Channel::State));
    TEST_ASSERT_EQUAL_STRING("event", channelName(Channel::Event));
    TEST_ASSERT_EQUAL_STRING("command", channelName(Channel::Command));
    TEST_ASSERT_EQUAL_STRING("receipt", channelName(Channel::Receipt));
    TEST_ASSERT_EQUAL_STRING("progress", channelName(Channel::Progress));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_duplicate_replays_latest_evidence_without_second_action);
    RUN_TEST(test_conflicting_signature_is_silent_and_keeps_original_evidence);
    RUN_TEST(test_stop_finishes_original_before_stop_and_idle_stop_succeeds);
    RUN_TEST(test_restart_preserves_receipt_but_removes_untrusted_running_claim);
    RUN_TEST(test_connect_decision_replays_full_state_and_only_known_progress);
    RUN_TEST(test_transport_policy_and_stop_slot_prevent_stop_starvation);
    RUN_TEST(test_event_cursor_advances_only_after_matching_persisted_puback);
    RUN_TEST(test_fixed_channel_qos_and_retain_policy_matches_adapter_contract);
    return UNITY_END();
}
