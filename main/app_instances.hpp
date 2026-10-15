#pragma once

/**
 * @file app_instances.hpp
 * @brief The instances main.cpp defines, for main's other composition files (the bench hooks,
 *        the stick task's I/O, the ports): the one UiApp and the drive session's adapter.
 *        Declarations only; main.cpp defines each once, at namespace scope.
 */

#include "drive_adapter.hpp"
#include "drive_ui/drive_port.hpp"
#include "hmi_ui/ui_app.hpp"

/// The one UiApp: every view the UI task draws, wired to its ports (main.cpp; constant
/// initialised, no global constructor). Its methods say which task may call them.
extern constinit hmi::ui::UiApp ui_app;

/// The drive session's adapter type: the session, its deadlines and latches, over drive_ui's
/// DrivePort on the UiApp's DriveUi.
using MainDriveAdapter = hmi::drive_adapter::DriveAdapter<hmi::ui::DrivePort<hmi::ui::DriveUi>>;

/// The one drive adapter (main.cpp, made at start-up: its constructor builds only the logger for
/// a corrupted state). Inputs and ticks on the UI task; the bench reads its atomic mirrors.
extern MainDriveAdapter drive_adapter;
