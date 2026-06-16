#include <cstring>

#include "unity.h"

#include "network_task.hpp"
#include "outbox.hpp"

namespace {

void SetName(logger::outbox::FileEntry& entry, const char* name, bool is_failure) {
    entry.filename.fill('\0');
    std::strncpy(entry.filename.data(), name, entry.filename.size() - 1U);
    entry.is_failure = is_failure;
}

} // namespace

TEST_CASE("outbox scan classifies legacy files and excludes active params", "[logger][outbox]") {
    bool is_failure = false;

    TEST_ASSERT_TRUE(logger::outbox::ClassifyPendingFilenameForUpload("params_0.jsonl", is_failure));
    TEST_ASSERT_FALSE(is_failure);

    TEST_ASSERT_TRUE(logger::outbox::ClassifyPendingFilenameForUpload("failure_0.jsonl", is_failure));
    TEST_ASSERT_TRUE(is_failure);

    TEST_ASSERT_FALSE(logger::outbox::ClassifyPendingFilenameForUpload("params_active_12345678_000001.jsonl",
                                                                       is_failure));
    TEST_ASSERT_FALSE(logger::outbox::ClassifyPendingFilenameForUpload("notes.txt", is_failure));
}

TEST_CASE("outbox upload filenames keep invalid-time files unique", "[logger][outbox]") {
    char first[logger::outbox::kMaxFilenameLen]{};
    char second[logger::outbox::kMaxFilenameLen]{};

    TEST_ASSERT_TRUE(logger::outbox::FormatUploadFilenameForTest("params",
                                                                 0U,
                                                                 0x1234abcdU,
                                                                 1U,
                                                                 first,
                                                                 sizeof(first)));
    TEST_ASSERT_TRUE(logger::outbox::FormatUploadFilenameForTest("params",
                                                                 0U,
                                                                 0x1234abcdU,
                                                                 2U,
                                                                 second,
                                                                 sizeof(second)));
    TEST_ASSERT_NOT_EQUAL(0, std::strcmp(first, second));
    TEST_ASSERT_NOT_NULL(std::strstr(first, "params_0_1234abcd_000001.jsonl"));
    TEST_ASSERT_NOT_NULL(std::strstr(second, "params_0_1234abcd_000002.jsonl"));
}

TEST_CASE("outbox sent collision filenames preserve source stem with suffix", "[logger][outbox]") {
    char sent[logger::outbox::kMaxFilenameLen]{};

    TEST_ASSERT_TRUE(logger::outbox::FormatSentCollisionFilenameForTest("params_0.jsonl",
                                                                        42U,
                                                                        sent,
                                                                        sizeof(sent)));
    TEST_ASSERT_EQUAL_STRING("params_0_sent_000042.jsonl", sent);
}

TEST_CASE("network publish selection keeps params pending before cadence", "[logger][network]") {
    logger::outbox::FileEntry entries[3]{};
    SetName(entries[0], "failure_0_boot_000001.jsonl", true);
    SetName(entries[1], "params_0_boot_000002.jsonl", false);
    SetName(entries[2], "failure_0_boot_000003.jsonl", true);

    logger::outbox::FileEntry selected[3]{};
    const std::size_t count = logger::network_task::SelectPublishEntriesForTest(entries,
                                                                                3U,
                                                                                false,
                                                                                selected,
                                                                                3U);

    TEST_ASSERT_EQUAL_size_t(2U, count);
    TEST_ASSERT_TRUE(selected[0].is_failure);
    TEST_ASSERT_TRUE(selected[1].is_failure);
}

TEST_CASE("network publish selection includes sealed params after cadence", "[logger][network]") {
    logger::outbox::FileEntry entries[2]{};
    SetName(entries[0], "failure_0_boot_000001.jsonl", true);
    SetName(entries[1], "params_0_boot_000002.jsonl", false);

    logger::outbox::FileEntry selected[2]{};
    const std::size_t count = logger::network_task::SelectPublishEntriesForTest(entries,
                                                                                2U,
                                                                                true,
                                                                                selected,
                                                                                2U);

    TEST_ASSERT_EQUAL_size_t(2U, count);
    TEST_ASSERT_TRUE(selected[0].is_failure);
    TEST_ASSERT_FALSE(selected[1].is_failure);
}
