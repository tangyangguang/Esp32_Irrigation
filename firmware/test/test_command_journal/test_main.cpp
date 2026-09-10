#include <unity.h>
#include <cstdio>
#include "irrigation/IrrigationCommandJournal.h"
using Journal = IrrigationCommandJournal;

static IrrigationIotProtocol::Command command(unsigned id, uint64_t issued=2000) {
    IrrigationIotProtocol::Command value{};
    std::snprintf(value.commandId,sizeof(value.commandId),"00000000-0000-4000-8000-%012u",id);
    value.kind=IrrigationIotProtocol::CommandKind::StartManual;
    value.signature=id; value.issuedAtMs=issued; value.expiresAtMs=issued+10000;
    return value;
}
static void test_reconnect_keeps_evidence_but_reboot_refuses_old_execution() {
    Journal ledger; TEST_ASSERT_TRUE(ledger.begin()); TEST_ASSERT_TRUE(ledger.observeTime(1000));
    const auto input=command(1); size_t index;
    TEST_ASSERT_TRUE(ledger.storeReceipt(input,Journal::ReceiptStatus::ACCEPTED,Journal::Reason::None,2000,true,2000,index));
    TEST_ASSERT_FALSE(ledger.storeFinal(index,Journal::ProgressStatus::SUCCEEDED,Journal::Reason::None,1999));
    TEST_ASSERT_TRUE(ledger.storeFinal(index,Journal::ProgressStatus::SUCCEEDED,Journal::Reason::None,3000));
    TEST_ASSERT_FALSE(ledger.storeFinal(index,Journal::ProgressStatus::FAILED,Journal::Reason::InternalState,4000));
    auto* stored=ledger.entry(index); TEST_ASSERT_NOT_NULL(stored);
    stored->receiptOrder=stored->progressOrder=0;
    ledger.replay();
    TEST_ASSERT_TRUE(stored->receiptOrder && stored->progressOrder);
    TEST_ASSERT_EQUAL_UINT64(2000,stored->receiptAtMs);
    TEST_ASSERT_EQUAL_UINT64(3000,stored->progressAtMs);
    size_t found;
    TEST_ASSERT_EQUAL_INT(int(Journal::LookupResult::SameCommand),int(ledger.lookup(input,4000,found)));
    auto conflict=input; ++conflict.signature;
    TEST_ASSERT_EQUAL_INT(int(Journal::LookupResult::ConflictingCommand),int(ledger.lookup(conflict,4000,found)));
    Journal reboot; TEST_ASSERT_TRUE(reboot.begin()); TEST_ASSERT_TRUE(reboot.observeTime(4000));
    TEST_ASSERT_FALSE(reboot.admit(input,4000)); TEST_ASSERT_NULL(reboot.entry(index));
}
static void test_expired_evidence_stays_until_delivered() {
    Journal ledger; ledger.begin(); ledger.observeTime(1000); size_t index;
    for(unsigned n=1;n<=Journal::kCapacity;++n) {
        TEST_ASSERT_TRUE(ledger.storeReceipt(command(n),Journal::ReceiptStatus::REJECTED,Journal::Reason::Busy,2000,false,2000,index));
    }
    auto next=command(50,20000);
    TEST_ASSERT_FALSE(ledger.storeReceipt(next,Journal::ReceiptStatus::REJECTED,Journal::Reason::Busy,20000,false,20000,index));
    ledger.entry(0)->receiptOrder=0;
    TEST_ASSERT_TRUE(ledger.storeReceipt(next,Journal::ReceiptStatus::REJECTED,Journal::Reason::Busy,20000,false,20000,index));
    TEST_ASSERT_EQUAL_size_t(0,index);
    TEST_ASSERT_TRUE(ledger.entry(1)->command.idEquals(command(2).commandId));
}
static void test_clock_rollback_and_first_second_do_not_admit_commands() {
    Journal ledger; ledger.begin(); TEST_ASSERT_TRUE(ledger.observeTime(1234));
    TEST_ASSERT_FALSE(ledger.admit(command(1,1999),2000));
    TEST_ASSERT_TRUE(ledger.admit(command(1,2000),2000));
    TEST_ASSERT_FALSE(ledger.admit(command(2,2000),1999));
    TEST_ASSERT_FALSE(ledger.observeTime(0));
    TEST_ASSERT_TRUE(ledger.admit(command(2,2001),2001));
}
static void test_missing_terminal_is_not_fabricated_or_reused() {
    Journal ledger; ledger.begin(); ledger.observeTime(1000); size_t index;
    for(unsigned n=1;n<=Journal::kCapacity;++n) {
        TEST_ASSERT_TRUE(ledger.storeReceipt(command(n),Journal::ReceiptStatus::ACCEPTED,Journal::Reason::None,2000,true,2000,index));
        TEST_ASSERT_TRUE(ledger.closeWithoutFinal(index));
        ledger.entry(index)->receiptOrder=0;
        TEST_ASSERT_EQUAL_INT(int(Journal::ProgressStatus::NONE),int(ledger.entry(index)->progress));
    }
    TEST_ASSERT_FALSE(ledger.storeReceipt(command(50,20000),Journal::ReceiptStatus::ACCEPTED,Journal::Reason::None,20000,true,20000,index));
}
int main(int,char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reconnect_keeps_evidence_but_reboot_refuses_old_execution);
    RUN_TEST(test_expired_evidence_stays_until_delivered);
    RUN_TEST(test_clock_rollback_and_first_second_do_not_admit_commands);
    RUN_TEST(test_missing_terminal_is_not_fabricated_or_reused);
    return UNITY_END();
}
