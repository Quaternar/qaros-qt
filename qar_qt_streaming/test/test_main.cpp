// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Custom gtest entry point: a QApplication must exist before any QWidget is
// constructed, and the capture tests deliberately exercise the REAL Qt paint
// path rather than a stub. The offscreen platform plugin keeps that headless —
// no window is ever created or shown, so running the suite cannot disturb a
// desktop session.

#include <gtest/gtest.h>

#include <QApplication>

int
main(int argc, char ** argv)
{
  // Set before QApplication is constructed, and only if the caller has not
  // chosen a platform themselves.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }

  QApplication app(argc, argv);
  (void)app;

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
