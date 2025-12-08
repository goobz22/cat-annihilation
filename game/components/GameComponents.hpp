#pragma once

/**
 * GameComponents.hpp
 *
 * Central header that includes all game-specific components
 * Include this file to access all component types
 */

// Core engine components
#include "../../engine/math/Transform.hpp"

// Game-specific components
#include "HealthComponent.hpp"
#include "MovementComponent.hpp"
#include "CombatComponent.hpp"
#include "combat_components.hpp"
#include "StoryComponents.hpp"

namespace CatGame {

// Forward declarations for commonly used types
struct HealthComponent;
struct MovementComponent;
struct CombatComponent;
struct BlockComponent;
struct DodgeComponent;
struct ComboComponent;
struct StatusEffectsComponent;

// Story mode components
struct StoryPlayerComponent;
struct ClanMemberComponent;
struct QuestGiverComponent;
struct ElementalAffinityComponent;
struct TerritoryOccupantComponent;
struct StorySkillsComponent;
struct MysticalConnectionComponent;
struct MentorComponent;

} // namespace CatGame
