#pragma once

#include "../k10_camera.h"

/* Board reset callback is the only dependency on the XL9535 output latch. */
void k10_gc2145_register(k10_camera_reset_fn reset);
