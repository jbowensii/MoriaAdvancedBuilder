// moria_dualsense.h — placeholder header (v6.23.0 cleanup)
//
// Original purpose: PS5 DualSense raw HID reader (DualSenseReader) +
// generic DirectInput gamepad reader (DIGamepadReader). Both classes
// were used by the controller dispatch loop in dllmain.cpp. That entire
// pipeline was gated behind m_controllerEnabled, which has been
// permanently false since v6.3.5 removed the F12 controller-enable
// checkbox UI (per controller-ui-removal.md).
//
// v6.22.3 wrapped DualSenseReader in #if 0 (Pass 1).
// v6.22.4 absorbed DualSenseReader and added DIGamepadReader to the
//   same Pass 1 gate, plus the entire dispatch loop, PE pre-hook
//   gamepad suppression, PE post-hook action-bar focus tracking, and
//   13 controller-related fields.
// v6.23.0 (this file) completes Pass 2 by deleting all of it.
//
// The DSState / DSConnection types and both reader classes were only
// referenced from inside the dead blocks, so removing the entire body
// is safe. The header itself stays because dllmain.cpp still includes
// it; removing the include is a separate trivial cleanup left for
// later if anyone notices.
//
// If controller support is ever desired again, see the v6.22.4 git
// commit (tag v6.22.4) — the entire infrastructure is preserved there.

#pragma once
