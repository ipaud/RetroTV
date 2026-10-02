#pragma once

// Forced into every host-test translation unit by tools/run_host_tests.sh (-include), before
// anything else. GCC reports two false positives from inside ArduinoJson that it finds after
// inlining, which -isystem does not hide: -Wmaybe-uninitialized (ObjectData::findKey) and, without
// sanitizers, -Waggressive-loop-optimizations (setTinyString, a branch that is never taken for long
// strings). Ignoring them around the library only keeps -Werror, and both warnings, for
// everything in src/, include/ and test/.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Waggressive-loop-optimizations"
#include <ArduinoJson.h>
#pragma GCC diagnostic pop
#endif
