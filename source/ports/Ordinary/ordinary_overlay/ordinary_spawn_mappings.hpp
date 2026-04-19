#pragma once

#include <cstdint>

// Copied mapping intent from Ordinary-NSMB-Mod:
// - SpookyController.cpp spawns actor 92 (Spooky Chaser)
//
// This overlay maps reserved custom IDs (0x03xx) to those ordinary actor IDs.
namespace ordinary_overlay {

constexpr uint16_t kCustomSpookyController = 0x0300;
constexpr uint16_t kCustomSpookyChaser = 0x0301;

constexpr uint16_t kOrdinarySpookyChaserActor = 92;

}  // namespace ordinary_overlay
