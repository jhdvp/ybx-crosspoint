#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include "ReaderSession.h"
#include "util/PluginEventQueue.h"
#include "util/ReaderSessionJournal.h"

namespace {

constexpr const char* JOURNAL = "/.crosspoint/reader-session.journal";
// Same cap as PluginEvents.cpp; the modelled outbox line is "<id> <duration>".
constexpr size_t OUTBOX_CAP = 4096;
// Same cadence as SESSION_CHECKPOINT_INTERVAL_MS in ReaderActivity.cpp.
constexpr uint32_t CHECKPOINT_INTERVAL_MS = 60 * 1000;

struct Files {
  std::map<std::string, std::string> data;
  bool exists(const std::string& path) { return data.count(path) != 0; }
  bool remove(const std::string& path) { return data.erase(path) != 0; }
  bool rename(const std::string& from, const std::string& to) {
    if (!exists(from) || exists(to)) return false;
    data[to] = data.at(from);
    data.erase(from);
    return true;
  }
  bool writeComplete(const std::string& path, std::string_view content) {
    data[path] = std::string(content);
    return true;
  }
  bool read(const std::string& path, std::string& out, size_t cap) {
    const auto it = data.find(path);
    if (it == data.end() || it->second.size() > cap) return false;
    out = it->second;
    return true;
  }
};

// Same shape as the firmware's checkpoint records, for preseeding the journal.
readersession::Record journalRecord(const char* id, const uint32_t durationSeconds) {
  readersession::Record out;
  std::snprintf(out.id, sizeof(out.id), "%s", id);
  out.startEpoch = 1730000000;
  out.durationSeconds = durationSeconds;
  out.startProgressBp = 1200;
  out.endProgressBp = 3400;
  out.book = "/books/ybx/42.epub";
  return out;
}

// Device-side lifecycle around the real ReaderSession, the real checkpoint
// format and the real FIFO trim/drain logic. Only the SD, the outbox line and
// the server store are modelled: the server keys its rows by event id, as
// YBX does with (device, event_id).
struct Device {
  Files files;
  ReaderSession session;
  std::string id;
  std::string outbox;
  bool online = false;
  std::set<std::string> server;
  int nextId = 0;
  // The two failure paths the journal exists for: a card that refuses writes
  // and an outbox that refuses an event.
  bool journalWritable = true;
  bool queueAccepted = true;
  uint32_t lastCheckpointMs = 0;
  int journalWrites = 0;

  // The id is allocated lazily, by the first checkpoint or flush that needs it.
  void openBook() { id.clear(); }

  void ensureId() {
    if (id.empty()) id = "0badc0de-" + std::to_string(++nextId);
  }

  void firstPage(uint32_t ms, int64_t epoch, int progress) { session.onRenderComplete(ms, epoch, progress); }

  void turnAndRender(bool succeeded, uint32_t ms, int64_t epoch, int progress) {
    session.noteTurn(succeeded);
    session.onRenderComplete(ms, epoch, progress);
  }

  readersession::Record recordFor(const ReaderSessionSummary& summary) const {
    readersession::Record record;
    std::snprintf(record.id, sizeof(record.id), "%s", id.c_str());
    record.startEpoch = summary.startEpoch;
    record.durationSeconds = summary.durationSeconds;
    record.startProgressBp = summary.startProgressBp;
    record.endProgressBp = summary.endProgressBp;
    record.book = "/books/ybx/42.epub";
    return record;
  }

  void queue(const readersession::Record& record) {
    outbox.append(record.id).append(" ").append(std::to_string(record.durationSeconds)).append("\n");
    trim();
  }

  // Mirrors ReaderActivity::checkpointReaderSession(), retry timer included.
  void checkpoint(uint32_t ms, int progress) {
    if (lastCheckpointMs != 0 && ms - lastCheckpointMs < CHECKPOINT_INTERVAL_MS) return;
    const ReaderSessionSummary summary = session.projected(ms, progress);
    if (summary.durationSeconds < ReaderSession::MIN_SESSION_SECONDS) return;
    ensureId();
    readersession::Record stale;
    if (readersession::read(files, JOURNAL, stale) && std::string(stale.id) != id) {
      lastCheckpointMs = ms;
      if (!recoverAtBoot()) return;
    }
    journalWrites++;
    if (journalWritable) readersession::write(files, JOURNAL, recordFor(summary));
    lastCheckpointMs = ms;
  }

  // Mirrors ReaderActivity::flushReaderSession(): sleep runs it from
  // prepareForSleep() and again from onExit(), so one pass must not undo the
  // other, and a checkpoint that is not this session's is left for a retry.
  void flush(uint32_t ms, int64_t epoch, int progress) {
    session.finish(ms, epoch, progress);
    if (session.isEmitWorthy()) {
      ensureId();
      readersession::Record stale;
      if (readersession::read(files, JOURNAL, stale) && std::string(stale.id) != id) {
        if (!recoverAtBoot()) return;
      }
      const readersession::Record record = recordFor(session.summary());
      if (queueAccepted) {
        queue(record);
        readersession::remove(files, JOURNAL);
      } else if (journalWritable) {
        readersession::write(files, JOURNAL, record);
      }
    } else if (!id.empty()) {
      readersession::remove(files, JOURNAL);
    }
    session.reset();
    id.clear();
    lastCheckpointMs = 0;
  }

  // Exit and sleep both end through flushReaderSession().
  void finishReading(uint32_t ms, int progress) { flush(ms, 1730000000, progress); }

  // Boot: the journal left by a crash is queued with its original id.
  bool recoverAtBoot(const bool clear = true) {
    readersession::Record record;
    if (!readersession::read(files, JOURNAL, record)) return false;
    if (!queueAccepted) return false;
    queue(record);
    if (clear) readersession::remove(files, JOURNAL);
    return true;
  }

  void trim() {
    if (outbox.size() > OUTBOX_CAP) pluginevents::trimQueueHead(outbox, OUTBOX_CAP);
  }

  // One drain pass; a failed delivery stays queued, as PluginEvents does.
  void drain() {
    size_t budget = 16;
    pluginevents::drainQueue(
        outbox, budget, [](std::string_view) { return false; },
        [this](std::string_view line) {
          if (!online) return false;
          const size_t space = line.find(' ');
          server.insert(std::string(line.substr(0, space)));
          return true;
        });
  }
};

TEST(ReaderSessionDelivery, ExitQueuesTheIntervalAndDeliversOnce) {
  Device device;
  device.openBook();
  device.firstPage(1000, 1730000000, 1000);
  device.turnAndRender(true, 31000, 1730000030, 1200);
  device.finishReading(41000, 1300);

  ASSERT_EQ(device.server.size(), 0u);
  device.online = true;
  device.drain();
  EXPECT_EQ(device.server.size(), 1u);
  EXPECT_TRUE(device.outbox.empty());
  EXPECT_TRUE(device.files.data.empty());
}

TEST(ReaderSessionDelivery, SleepWithOpenBookKeepsTheQueuedSessionAcrossABoot) {
  Device device;
  device.openBook();
  device.firstPage(1000, 1730000000, 1000);
  device.turnAndRender(true, 21000, 1730000020, 1200);
  device.finishReading(51000, 1500);  // prepareForSleep() runs before WiFi is up

  ASSERT_EQ(device.server.size(), 0u);
  EXPECT_NE(device.outbox.find("0badc0de-1 50"), std::string::npos);

  // The outbox lives on the card, not in RAM: the reboot keeps the line.
  Device rebooted;
  rebooted.outbox = device.outbox;
  rebooted.online = true;
  rebooted.drain();
  EXPECT_EQ(rebooted.server.size(), 1u);
}

TEST(ReaderSessionDelivery, RestartWithOpenBookRecoversTheCheckpointWithTheSameId) {
  Device device;
  device.openBook();
  device.firstPage(1000, 1730000000, 1000);
  device.turnAndRender(true, 31000, 1730000030, 1200);
  device.checkpoint(31000, 1200);
  ASSERT_TRUE(device.files.exists(JOURNAL));

  // Crash or reboot: RAM is gone, the checkpoint is not.
  Device rebooted;
  rebooted.files = device.files;
  ASSERT_TRUE(rebooted.recoverAtBoot());
  EXPECT_NE(rebooted.outbox.find("0badc0de-1 30"), std::string::npos);

  rebooted.online = true;
  rebooted.drain();
  EXPECT_EQ(rebooted.server.size(), 1u);
  EXPECT_TRUE(rebooted.files.data.empty());
}

TEST(ReaderSessionDelivery, OfflineThenOnlineDeliversTheBacklog) {
  Device device;
  device.openBook();
  device.firstPage(1000, 1730000000, 1000);
  device.turnAndRender(true, 41000, 1730000040, 1200);
  device.finishReading(51000, 1300);

  device.drain();  // offline: the line stays
  EXPECT_TRUE(device.server.empty());
  EXPECT_FALSE(device.outbox.empty());

  device.online = true;
  device.drain();
  device.drain();
  EXPECT_EQ(device.server.size(), 1u);
  EXPECT_TRUE(device.outbox.empty());
}

TEST(ReaderSessionDelivery, DuplicateDeliveryIsIdempotent) {
  Device device;
  device.openBook();
  device.firstPage(1000, 1730000000, 1000);
  device.turnAndRender(true, 31000, 1730000030, 1200);
  device.checkpoint(31000, 1200);

  // Recover twice, as if the journal delete failed and the checkpoint
  // survived to the next boot.
  ASSERT_TRUE(device.recoverAtBoot(false));
  ASSERT_TRUE(device.recoverAtBoot(false));
  device.online = true;
  device.drain();

  EXPECT_EQ(device.server.size(), 1u);  // same id: one row
  EXPECT_TRUE(device.outbox.empty());
}

TEST(ReaderSessionDelivery, FullOutboxDropsOldestSessionsAndKeepsTheNewest) {
  Device device;
  device.online = true;
  for (int i = 1; i <= 400; i++) {
    device.id = "0badc0de-" + std::to_string(i);
    device.outbox += device.id + " " + std::to_string(600 + i) + "\n";
    device.trim();
  }
  EXPECT_LE(device.outbox.size(), OUTBOX_CAP);
  EXPECT_EQ(device.outbox.find("0badc0de-1 "), std::string::npos);
  EXPECT_NE(device.outbox.find("0badc0de-400 "), std::string::npos);

  // One drain pass delivers at most `budget` events; a backlog takes passes.
  for (int pass = 0; pass < 100 && !device.outbox.empty(); pass++) device.drain();
  EXPECT_EQ(device.server.count("0badc0de-400"), 1u);
  EXPECT_TRUE(device.outbox.empty());
}

TEST(ReaderSessionDelivery, QuickFlipIsNotReported) {
  Device device;
  device.openBook();
  device.firstPage(1000, 1730000000, 1000);
  device.turnAndRender(true, 2500, 1730000001, 1100);
  device.finishReading(4000, 1100);

  EXPECT_TRUE(device.outbox.empty());
  EXPECT_TRUE(device.files.data.empty());
}

TEST(ReaderSessionDelivery, SecondFlushOfTheSleepPathKeepsTheRetryCheckpoint) {
  Device device;
  device.openBook();
  device.firstPage(1000, 1730000000, 1000);
  device.turnAndRender(true, 21000, 1730000020, 1200);
  device.queueAccepted = false;  // the outbox refuses: the session waits in the journal
  device.flush(51000, 1730000050, 1500);
  ASSERT_TRUE(device.files.exists(JOURNAL));

  device.flush(51000, 1730000050, 1500);  // the same sleep path then reaches onExit()

  readersession::Record kept;
  ASSERT_TRUE(readersession::read(device.files, JOURNAL, kept));
  EXPECT_STREQ(kept.id, "0badc0de-1");
  EXPECT_EQ(kept.durationSeconds, 50u);

  Device rebooted;
  rebooted.files = device.files;
  rebooted.online = true;
  ASSERT_TRUE(rebooted.recoverAtBoot());
  rebooted.drain();
  EXPECT_EQ(rebooted.server.count("0badc0de-1"), 1u);
}

TEST(ReaderSessionDelivery, AFailedRecoveryKeepsTheOtherSessionsCheckpoint) {
  Device device;
  device.files.data[JOURNAL] = readersession::serialize(journalRecord("deadbeef-1", 900));
  device.openBook();
  device.firstPage(1000, 1730000000, 1000);
  device.turnAndRender(true, 21000, 1730000020, 1200);
  device.queueAccepted = false;  // queuing the old record fails, so nothing may replace it
  device.flush(51000, 1730000050, 1500);

  readersession::Record kept;
  ASSERT_TRUE(readersession::read(device.files, JOURNAL, kept));
  EXPECT_STREQ(kept.id, "deadbeef-1");
  EXPECT_EQ(kept.durationSeconds, 900u);
  EXPECT_TRUE(device.outbox.empty());
}

TEST(ReaderSessionDelivery, FailingCheckpointIsRetriedOnceAMinute) {
  Device device;
  device.journalWritable = false;  // a full or write-protected card
  device.openBook();
  device.firstPage(1000, 1730000000, 1000);
  device.turnAndRender(true, 61000, 1730000060, 1200);
  for (uint32_t ms = 61000; ms < 241000; ms += 100) device.checkpoint(ms, 1200);

  EXPECT_EQ(device.journalWrites, 3);  // at 61 s, 121 s and 181 s, not every loop pass
}

}  // namespace
