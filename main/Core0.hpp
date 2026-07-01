#pragma once

// =============================
// Core0 Include Surface
// =============================
// Core0 umbrella include.
// Keep this file tiny so call-sites can include one header for all real-time DSP surfaces
// without pulling unrelated Core1/control-plane modules.
#include "Core0_State.hpp"
#include "Core0_Protection.hpp"
#include "Core0_Pipeline.hpp"
