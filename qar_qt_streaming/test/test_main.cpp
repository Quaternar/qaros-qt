// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Custom gtest entry point: a QApplication must exist before any QWidget is
// constructed, and the capture tests deliberately exercise the REAL Qt paint
// path rather than a stub. The offscreen platform plugin keeps that headless —
// no window is ever created or shown, so running the suite cannot disturb a
// desktop session.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_compat.hpp"

#include <QApplication>

int
main(int argc, char ** argv)
{
  // Set before QApplication is constructed, and only if the caller has not
  // chosen a platform themselves.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }

  // A fractional device pixel ratio (QT_SCALE_FACTOR=1.25) reaches the widgets only if the
  // rounding policy lets it through, and that is set before the QApplication exists. The runs
  // of the fractional-ratio tests ask for it; every other run keeps the defaults of Qt.
  if (qEnvironmentVariableIsSet("QAR_QT_TESTS_FRACTIONAL_DPR")) {
    qar_qt_streaming::qt_compat::enable_fractional_high_dpi();
  }

  QApplication app(argc, argv);
  (void)app;

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
