#include <unity.h>

#include <array>
#include <cstdio>
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

struct PersistenceHarness {
    EvidenceStore* live = nullptr;
    EvidenceStore durable{};
    bool succeed = true;
    uint8_t calls = 0;
};

bool persistEvidence(void* context) {
    auto& harness = *static_cast<PersistenceHarness*>(context);
    ++harness.calls;
    if (!harness.succeed) return false;
    harness.durable.entries() = harness.live->entries();
    return true;
}

CommandGuardContext stopContext(uint64_t currentTimeMs, uint64_t expiresAtMs) {
    CommandGuardContext context{};
    context.capability = Capability::Stop;
    context.currentTimeMs = currentTimeMs;
    context.expiresAtMs = expiresAtMs;
    context.businessReady = true;
    context.configurationReady = true;
    context.normalWatering = true;
    return context;
}

void test_trusted_anchor_allows_only_safe_commands_after_sync_loss() {
    TrustedTimeAnchor anchor;
    constexpr uint32_t kEpoch = 1700000000U;
    anchor.observe(true, kEpoch, 1000U);
    anchor.observe(false, 0, 6500U);
    const uint64_t projected = anchor.currentTimeMs(6500U);
    TEST_ASSERT_EQUAL_UINT64(1700000005500ULL, projected);
    anchor.observe(true, kEpoch - 10U, 7000U);
    TEST_ASSERT_EQUAL_UINT64(1700000006000ULL, anchor.currentTimeMs(7000U));

    CommandGuardContext stop = stopContext(projected, projected + 5000U);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandGuardRejection::None),
                      static_cast<int>(evaluateCommandGuard(stop)));
    stop.expiresAtMs = projected;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandGuardRejection::Expired),
                      static_cast<int>(evaluateCommandGuard(stop)));

    TrustedTimeAnchor neverTrusted;
    stop = stopContext(neverTrusted.currentTimeMs(7000U), projected + 5000U);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandGuardRejection::TimeUntrusted),
                      static_cast<int>(evaluateCommandGuard(stop)));

    CommandGuardContext paused = stopContext(projected, projected + 5000U);
    paused.capability = Capability::AutomaticWatering;
    paused.automaticMode = AutomaticWateringMode::PausedUntil;
    paused.resumeAtEpoch = static_cast<uint32_t>(projected / 1000ULL) + 60U;
    paused.calendarTimeTrusted = false;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandGuardRejection::TimeUntrusted),
                      static_cast<int>(evaluateCommandGuard(paused)));

    stop = stopContext(projected, projected + 5000U);
    stop.wateringActive = true;
    stop.normalWatering = false;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandGuardRejection::MaintenanceActivity),
                      static_cast<int>(evaluateCommandGuard(stop)));
}

void test_receipt_persistence_gates_publish_action_retry_and_restart() {
    EvidenceStore store;
    PersistenceHarness persistence{&store};
    uint8_t publishedReceipts = 0;
    uint8_t businessActions = 0;

    EvidenceEntry& first = admitted(store, kCommandA, 0x01);
    persistence.succeed = false;
    TEST_ASSERT_EQUAL(
        static_cast<int>(EvidenceCommitResult::PersistenceFailed),
        static_cast<int>(store.commitReceipt(
            first, EvidenceStatus::Accepted, nullptr, persistEvidence, &persistence)));
    TEST_ASSERT_NULL(store.find(kCommandA));
    TEST_ASSERT_EQUAL_UINT8(0, publishedReceipts);
    TEST_ASSERT_EQUAL_UINT8(0, businessActions);

    EvidenceEntry& retry = admitted(store, kCommandA, 0x01);
    persistence.succeed = true;
    TEST_ASSERT_EQUAL(
        static_cast<int>(EvidenceCommitResult::Persisted),
        static_cast<int>(store.commitReceipt(
            retry, EvidenceStatus::Accepted, nullptr, persistEvidence, &persistence)));
    ++publishedReceipts;
    ++businessActions;
    TEST_ASSERT_EQUAL(
        static_cast<int>(EvidenceCommitResult::Persisted),
        static_cast<int>(store.commitProgress(
            retry, EvidenceStatus::Running, nullptr, persistEvidence, &persistence)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(EvidenceCommitResult::Persisted),
        static_cast<int>(store.commitProgress(
            retry, EvidenceStatus::Succeeded, nullptr, persistEvidence, &persistence)));

    const auto duplicate = store.admit(kCommandA, signature(0x01), 2000,
                                       Capability::StartManual, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(Admission::Replay),
                      static_cast<int>(duplicate.result));
    ++publishedReceipts;
    TEST_ASSERT_EQUAL_UINT8(1, businessActions);

    EvidenceStore restarted;
    restarted.entries() = persistence.durable.entries();
    TEST_ASSERT_TRUE(restarted.validate());
    const auto afterRestart = restarted.admit(kCommandA, signature(0x01), 2000,
                                               Capability::StartManual, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(Admission::Replay),
                      static_cast<int>(afterRestart.result));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Succeeded),
                      static_cast<int>(afterRestart.entry->progress));
    TEST_ASSERT_EQUAL_UINT8(1, businessActions);
    TEST_ASSERT_EQUAL_UINT8(2, publishedReceipts);
}

void test_unpersisted_rejection_and_terminal_are_never_replayed() {
    EvidenceStore store;
    PersistenceHarness persistence{&store};
    EvidenceEntry& rejected = admitted(store, kCommandA, 0x51);
    persistence.succeed = false;
    TEST_ASSERT_EQUAL(
        static_cast<int>(EvidenceCommitResult::PersistenceFailed),
        static_cast<int>(store.commitReceipt(
            rejected, EvidenceStatus::Rejected, "expired", persistEvidence,
            &persistence)));
    TEST_ASSERT_NULL(store.find(kCommandA));

    EvidenceEntry& running = admitted(store, kCommandB, 0x52);
    persistence.succeed = true;
    TEST_ASSERT_EQUAL(
        static_cast<int>(EvidenceCommitResult::Persisted),
        static_cast<int>(store.commitReceipt(
            running, EvidenceStatus::Accepted, nullptr, persistEvidence, &persistence)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(EvidenceCommitResult::Persisted),
        static_cast<int>(store.commitProgress(
            running, EvidenceStatus::Running, nullptr, persistEvidence, &persistence)));
    persistence.succeed = false;
    TEST_ASSERT_EQUAL(
        static_cast<int>(EvidenceCommitResult::PersistenceFailed),
        static_cast<int>(store.commitProgress(
            running, EvidenceStatus::Succeeded, nullptr, persistEvidence,
            &persistence)));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Running),
                      static_cast<int>(running.progress));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::Running),
                      static_cast<int>(persistence.durable.find(kCommandB)->progress));
}

void test_unpersisted_running_evidence_gates_business_action() {
    EvidenceStore store;
    PersistenceHarness persistence{&store};
    EvidenceEntry& entry = admitted(store, kCommandA, 0x53);
    TEST_ASSERT_EQUAL(
        static_cast<int>(EvidenceCommitResult::Persisted),
        static_cast<int>(store.commitReceipt(
            entry, EvidenceStatus::Accepted, nullptr, persistEvidence, &persistence)));

    uint8_t businessActions = 0;
    persistence.succeed = false;
    const auto running = store.commitProgress(
        entry, EvidenceStatus::Running, nullptr, persistEvidence, &persistence);
    if (running == EvidenceCommitResult::Persisted) ++businessActions;

    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceCommitResult::PersistenceFailed),
                      static_cast<int>(running));
    TEST_ASSERT_EQUAL_UINT8(0, businessActions);
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::None),
                      static_cast<int>(entry.progress));
    TEST_ASSERT_EQUAL(static_cast<int>(EvidenceStatus::None),
                      static_cast<int>(persistence.durable.find(kCommandA)->progress));
}

void test_full_evidence_store_silently_retries_without_action() {
    EvidenceStore store;
    PersistenceHarness persistence{&store};
    char commandId[37]{};
    for (uint8_t index = 0; index < kEvidenceCapacity; ++index) {
        std::snprintf(commandId, sizeof(commandId),
                      "%08x-0000-4000-8000-%012x", index + 1U, index + 1U);
        const auto result = store.admit(commandId, signature(index), 60000,
                                        Capability::StartManual, 1000);
        TEST_ASSERT_EQUAL(static_cast<int>(Admission::New),
                          static_cast<int>(result.result));
        TEST_ASSERT_EQUAL(
            static_cast<int>(EvidenceCommitResult::Persisted),
            static_cast<int>(store.commitReceipt(
                *result.entry, EvidenceStatus::Rejected, "busy", persistEvidence,
                &persistence)));
    }
    const auto full = store.admit(kStopCommand, signature(0x61), 60000,
                                  Capability::Stop, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(Admission::Full),
                      static_cast<int>(full.result));
    TEST_ASSERT_NULL(full.entry);
    const auto retried = store.admit(kStopCommand, signature(0x61), 60000,
                                     Capability::Stop, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(Admission::Full),
                      static_cast<int>(retried.result));
    TEST_ASSERT_NULL(store.find(kStopCommand));

    EvidenceStore restarted;
    restarted.entries() = persistence.durable.entries();
    TEST_ASSERT_TRUE(restarted.validate());
    const auto afterRestart = restarted.admit(kStopCommand, signature(0x61),
                                               60000, Capability::Stop, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(Admission::Full),
                      static_cast<int>(afterRestart.result));
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
    RUN_TEST(test_trusted_anchor_allows_only_safe_commands_after_sync_loss);
    RUN_TEST(test_receipt_persistence_gates_publish_action_retry_and_restart);
    RUN_TEST(test_unpersisted_rejection_and_terminal_are_never_replayed);
    RUN_TEST(test_unpersisted_running_evidence_gates_business_action);
    RUN_TEST(test_full_evidence_store_silently_retries_without_action);
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
