// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// LatestVideoFrame: the thread-safe single-slot mailbox between a decode
// worker and the GUI thread (docs/ARCHITECTURE.md#thread-model). No QWidget
// involved, but this still runs inside
// qar_qt_streaming_tests' QApplication-backed process like everything else.

#include <gtest/gtest.h>

#include "qar_qt_streaming/latest_video_frame.hpp"

#include <QColor>
#include <QImage>
#include <QSize>

#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>

using qar_qt_streaming::LatestVideoFrame;

TEST(LatestVideoFrame, StartsEmptyWithZeroSequenceAndHealthyStatus)
{
  LatestVideoFrame mailbox;
  EXPECT_EQ(mailbox.latest(), nullptr);
  EXPECT_EQ(mailbox.sequence(), 0u);
  EXPECT_EQ(mailbox.fps(), 0.0);
  EXPECT_TRUE(mailbox.status().empty());
}

TEST(LatestVideoFrame, PublishIsReadableAndBumpsSequence)
{
  LatestVideoFrame mailbox;
  QImage frame(4, 3, QImage::Format_RGBA8888);
  frame.fill(Qt::red);
  mailbox.publish(frame);

  const std::shared_ptr<const QImage> latest = mailbox.latest();
  ASSERT_NE(latest, nullptr);
  EXPECT_EQ(latest->size(), QSize(4, 3));
  EXPECT_EQ(mailbox.sequence(), 1u);

  mailbox.publish(QImage(2, 2, QImage::Format_RGBA8888));
  EXPECT_EQ(mailbox.sequence(), 2u);
  EXPECT_EQ(mailbox.latest()->size(), QSize(2, 2));
}

TEST(LatestVideoFrame, StatusRoundTripsAndIsClearedByAPublish)
{
  LatestVideoFrame mailbox;
  mailbox.set_status("Waiting for video on /cam/image_raw...");
  EXPECT_EQ(mailbox.status(), "Waiting for video on /cam/image_raw...");

  mailbox.publish(QImage(2, 2, QImage::Format_RGBA8888));
  EXPECT_TRUE(mailbox.status().empty()) << "a frame arriving means the wait it described is over";

  mailbox.set_status("decode error: bad encoding");
  EXPECT_EQ(mailbox.status(), "decode error: bad encoding");
}

TEST(LatestVideoFrame, FpsIsZeroUntilASecondFrameGivesAnInterval)
{
  LatestVideoFrame mailbox;
  EXPECT_EQ(mailbox.fps(), 0.0);
  mailbox.publish(QImage(2, 2, QImage::Format_RGBA8888));
  EXPECT_EQ(mailbox.fps(), 0.0) << "one sample has no interval to measure yet";
  mailbox.publish(QImage(2, 2, QImage::Format_RGBA8888));
  EXPECT_GT(mailbox.fps(), 0.0);
}

TEST(LatestVideoFrame, ConcurrentPublishAndReadIsSafeAndSequenceIsMonotonic)
{
  LatestVideoFrame mailbox;
  constexpr std::uint64_t kFrameCount = 1000;

  std::thread publisher([&mailbox, kFrameCount]() {
    for (std::uint64_t i = 0; i < kFrameCount; ++i) {
      QImage frame(2, 2, QImage::Format_RGBA8888);
      frame.fill(QColor(static_cast<int>(i % 256), 0, 0));
      mailbox.publish(std::move(frame));
    }
  });

  std::uint64_t previous = 0;
  bool saw_nonmonotonic = false;
  std::uint64_t seq = 0;
  do {
    seq = mailbox.sequence();
    if (seq < previous) {
      saw_nonmonotonic = true;
    }
    previous = seq;
    // Read concurrently with the publisher; must never crash or hand back a
    // frame that is mid-mutation (see the header comment on why `shared_ptr
    // to a const QImage` is what crosses threads here).
    const std::shared_ptr<const QImage> frame = mailbox.latest();
    if (frame) {
      EXPECT_EQ(frame->size(), QSize(2, 2));
    }
  } while (seq < kFrameCount);

  publisher.join();

  EXPECT_FALSE(saw_nonmonotonic) << "sequence must never go backwards";
  EXPECT_EQ(mailbox.sequence(), kFrameCount);
  ASSERT_NE(mailbox.latest(), nullptr);
}
