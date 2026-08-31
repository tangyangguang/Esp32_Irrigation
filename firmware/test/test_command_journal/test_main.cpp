#include <Preferences.h>
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "irrigation/IrrigationCommandJournal.h"

namespace {

IrrigationIotProtocol::Command makeCommand(unsigned id,
                                           uint64_t signature,
                                           uint64_t expiresAtMs = 50000) {
    IrrigationIotProtocol::Command command;
    std::snprintf(command.commandId,
                  sizeof(command.commandId),
                  "00000000-0000-4000-8000-%012u",
                  id);
    command.kind = IrrigationIotProtocol::CommandKind::StartManual;
    command.signature = signature;
    command.expiresAtMs = expiresAtMs;
    return command;
}

void test_receipt_and_final_survive_restart_and_conflict_is_detected() {
    Preferences::testReset();
    IrrigationCommandJournal journal;
    TEST_ASSERT_TRUE(journal.begin());
    const auto command = makeCommand(1, 0x1234);
    std::size_t index = IrrigationCommandJournal::kCapacity;
    TEST_ASSERT_TRUE(journal.storeReceipt(
        command,
        IrrigationCommandJournal::ReceiptStatus::Accepted,
        IrrigationCommandJournal::Reason::None,
        1000,
        true,
        1000,
        index));
    TEST_ASSERT_TRUE(journal.storeFinal(
        index,
        IrrigationCommandJournal::ProgressStatus::Succeeded,
        IrrigationCommandJournal::Reason::None,
        2000));

    IrrigationCommandJournal restarted;
    TEST_ASSERT_TRUE(restarted.begin());
    std::size_t found = IrrigationCommandJournal::kCapacity;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(IrrigationCommandJournal::LookupResult::SameCommand),
        static_cast<int>(restarted.lookup(command, 3000, found)));
    TEST_ASSERT_EQUAL_size_t(index, found);
    const auto* entry = restarted.entry(found);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(IrrigationCommandJournal::ProgressStatus::Succeeded),
        static_cast<int>(entry->progress));

    auto conflicting = command;
    conflicting.signature++;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            IrrigationCommandJournal::LookupResult::ConflictingCommand),
        static_cast<int>(restarted.lookup(conflicting, 3000, found)));
}

void test_restart_closes_interrupted_process_without_inventing_final_state() {
    Preferences::testReset();
    IrrigationCommandJournal journal;
    TEST_ASSERT_TRUE(journal.begin());
    const auto command = makeCommand(2, 0x5678);
    std::size_t index = IrrigationCommandJournal::kCapacity;
    TEST_ASSERT_TRUE(journal.storeReceipt(
        command,
        IrrigationCommandJournal::ReceiptStatus::Accepted,
        IrrigationCommandJournal::Reason::None,
        1000,
        true,
        1000,
        index));

    IrrigationCommandJournal restarted;
    TEST_ASSERT_TRUE(restarted.begin());
    const auto* entry = restarted.entry(index);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_FALSE(entry->processOpen);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(IrrigationCommandJournal::ProgressStatus::None),
        static_cast<int>(entry->progress));

    IrrigationCommandJournal restartedAgain;
    TEST_ASSERT_TRUE(restartedAgain.begin());
    entry = restartedAgain.entry(index);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_FALSE(entry->processOpen);
    TEST_ASSERT_EQUAL_UINT64(0, entry->progressObservedAtMs);
}

void test_close_without_final_is_persistent() {
    Preferences::testReset();
    IrrigationCommandJournal journal;
    TEST_ASSERT_TRUE(journal.begin());
    const auto command = makeCommand(3, 0x9ABC);
    std::size_t index = IrrigationCommandJournal::kCapacity;
    TEST_ASSERT_TRUE(journal.storeReceipt(
        command,
        IrrigationCommandJournal::ReceiptStatus::Accepted,
        IrrigationCommandJournal::Reason::None,
        1000,
        true,
        1000,
        index));
    TEST_ASSERT_TRUE(journal.closeWithoutFinal(index));

    IrrigationCommandJournal restarted;
    TEST_ASSERT_TRUE(restarted.begin());
    const auto* entry = restarted.entry(index);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_FALSE(entry->processOpen);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(IrrigationCommandJournal::ProgressStatus::None),
        static_cast<int>(entry->progress));
}

void test_capacity_never_overwrites_unexpired_entry_and_reuses_expired_entry() {
    Preferences::testReset();
    IrrigationCommandJournal journal;
    TEST_ASSERT_TRUE(journal.begin());
    for (std::size_t count = 0; count < IrrigationCommandJournal::kCapacity;
         ++count) {
        const auto command = makeCommand(static_cast<unsigned>(count + 1U),
                                         count + 100U,
                                         50000);
        std::size_t index = IrrigationCommandJournal::kCapacity;
        TEST_ASSERT_TRUE(journal.storeReceipt(
            command,
            IrrigationCommandJournal::ReceiptStatus::Rejected,
            IrrigationCommandJournal::Reason::Busy,
            1000,
            false,
            1000,
            index));
    }
    const auto overflow = makeCommand(99, 999, 80000);
    std::size_t index = IrrigationCommandJournal::kCapacity;
    TEST_ASSERT_FALSE(journal.storeReceipt(
        overflow,
        IrrigationCommandJournal::ReceiptStatus::Rejected,
        IrrigationCommandJournal::Reason::Busy,
        2000,
        false,
        2000,
        index));
    TEST_ASSERT_TRUE(journal.storeReceipt(
        overflow,
        IrrigationCommandJournal::ReceiptStatus::Rejected,
        IrrigationCommandJournal::Reason::Busy,
        60000,
        false,
        60000,
        index));
    TEST_ASSERT_LESS_THAN_size_t(IrrigationCommandJournal::kCapacity, index);
}

void test_crc_corruption_fails_closed() {
    Preferences::testReset();
    IrrigationCommandJournal journal;
    TEST_ASSERT_TRUE(journal.begin());
    const auto command = makeCommand(4, 0xDEAD);
    std::size_t index = IrrigationCommandJournal::kCapacity;
    TEST_ASSERT_TRUE(journal.storeReceipt(
        command,
        IrrigationCommandJournal::ReceiptStatus::Accepted,
        IrrigationCommandJournal::Reason::None,
        1000,
        false,
        1000,
        index));
    TEST_ASSERT_TRUE(Preferences::testCorrupt("irr_iot_cmd", "journal"));

    IrrigationCommandJournal corrupted;
    TEST_ASSERT_FALSE(corrupted.begin());
    TEST_ASSERT_FALSE(corrupted.ready());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_receipt_and_final_survive_restart_and_conflict_is_detected);
    RUN_TEST(test_restart_closes_interrupted_process_without_inventing_final_state);
    RUN_TEST(test_close_without_final_is_persistent);
    RUN_TEST(test_capacity_never_overwrites_unexpired_entry_and_reuses_expired_entry);
    RUN_TEST(test_crc_corruption_fails_closed);
    return UNITY_END();
}
