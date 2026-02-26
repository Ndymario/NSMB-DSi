#pragma once

#include <cstdint>

// Copied mapping intent from Ordinary-NSMB-Mod:
// - SpookyController.cpp spawns actor 92 (Spooky Chaser)
// - SpookyBoss.cpp spawns actor 280 (BlockProjectile)
//
// This overlay maps reserved custom IDs (0x03xx) to those ordinary actor IDs.
namespace ordinary_overlay {

constexpr uint16_t kCustomSpookyController = 0x0300;
constexpr uint16_t kCustomSpookyChaser = 0x0301;
constexpr uint16_t kCustomBossBlock = 0x0302;

constexpr uint16_t kOrdinarySpookyChaserActor = 92;
constexpr uint16_t kOrdinaryBossBlockActor = 280;

}  // namespace ordinary_overlay
