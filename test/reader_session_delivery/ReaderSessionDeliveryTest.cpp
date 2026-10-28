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

  void openBook() { id = "0badc0de-" + std::to_string(++nextId); }

  void firstPage(uint32_t ms, int64_t epoch, int progress) { session.onRenderComplete(ms, epoch, progress); }

  void turnAndRender(bool succeeded, uint32_t ms, int64_t epoch, int progress) {
    session.noteTurn(succeeded);
    session.onRenderComplete(ms, epoch, progress);
  }

  void checkpoint(uint32_t ms, int progress) {
    const ReaderSessionSummary summary = session.projected(ms, progress);
    if (summary.durationSeconds < ReaderSession::MIN_SESSION_SECONDS) return;
    readersession::Record record;
    std::snprintf(record.id, sizeof(record.id), "%s", id.c_str());
    record.startEpoch = summary.startEpoch;
    record.durationSeconds = summary.durationSeconds;
    record.startProgressBp = summary.startProgressBp;
    record.endProgressBp = summary.endProgressBp;
    record.book = "/books/ybx/42.epub";
    readersession::write(files, JOURNAL, record);
  }

  // Exit and sleep both end through ReaderActivity::flushReaderSession().
  void finishReading(uint32_t ms, int progress) {
    session.finish(ms, 1730000000, progress);
    if (session.isEmitWorthy()) {
      outbox += id + " " + std::to_string(session.summary().durationSeconds) + "\n";
      trim();
    }
    readersession::remove(files, JOURNAL);
    session.reset();
    id.clear();
  }

  // Boot: the journal left by a crash is queued with its original id.
  bool recoverAtBoot(const bool clear = true) {
    readersession::Record record;
    if (!readersession::read(files, JOURNAL, record)) return false;
    outbox.append(record.id).append(" ").append(std::to_string(record.durationSeconds)).append("\n");
    trim();
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

}  // namespace
