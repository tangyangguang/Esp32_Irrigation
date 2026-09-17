#pragma once

#include <Esp32Base.h>
#include <stdio.h>
#include <runtime/Esp32BaseRecordStore.h>

// Helpers used by business record stores to recover from on-flash layouts that
// cannot serve the current firmware: a blank LittleFS after a first flash, or
// a store version/layout change after OTA. Each store lives in its own
// versioned directory (/esp32base/records/<type>.v<n>), so clearing only that
// directory discards nothing else. Write faults (free space etc.) are not
// reset here.

namespace IrrigationRecordStoreRecovery {

constexpr const char* kRecordStoreRoot = "/esp32base/records";

struct CleanupContext {
    char directory[Esp32BaseRecordStore::MAX_STORE_PATH_LENGTH]{};
    bool cleanupFailed = false;
};

inline void cleanupEntry(const Esp32BaseFs::EntryInfo& entry, void* user) {
    auto* context = static_cast<CleanupContext*>(user);
    char path[Esp32BaseRecordStore::MAX_STORE_PATH_LENGTH];
    const int written = snprintf(path, sizeof(path), "%s/%s",
                                 context->directory, entry.name);
    if (written <= 0 ||
        static_cast<size_t>(written) >= sizeof(path)) {
        context->cleanupFailed = true;
        return;
    }
    // Stores are flat version directories: control header plus .seg/.tmp files.
    if (entry.isDir || !Esp32BaseFs::removeFile(path)) {
        context->cleanupFailed = true;
    }
}

inline bool resetStructuralStore(
    Esp32BaseRecordStore& store,
    const char* recordTypeName,
    uint16_t storeVersion,
    const Esp32BaseRecordStore::StoreDefinition& definition) {
    Esp32BaseRecordStore::StoreStatus status;
    if (!store.readStatus(status) || status.ready ||
        (status.state != Esp32BaseRecordStore::StoreState::StructuralFault &&
         status.state != Esp32BaseRecordStore::StoreState::Uninitialized)) {
        return false;
    }
    CleanupContext context;
    if (snprintf(context.directory, sizeof(context.directory), "%s/%s.v%u",
                 kRecordStoreRoot, recordTypeName,
                 static_cast<unsigned>(storeVersion)) >=
        static_cast<int>(sizeof(context.directory))) {
        return false;
    }
    if (Esp32BaseFs::exists(context.directory)) {
        if (!Esp32BaseFs::listDirInfo(context.directory, cleanupEntry,
                                      &context) ||
            context.cleanupFailed ||
            !Esp32BaseFs::rmdir(context.directory)) {
            return false;
        }
    }
    return store.begin(definition);
}

}  // namespace IrrigationRecordStoreRecovery
