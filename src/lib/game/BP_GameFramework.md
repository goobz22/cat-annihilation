# Game State Management System in Unreal Engine

This document outlines how to implement the game state management system in Unreal Engine, replacing the Zustand store implementation from the React/Three.js version.

## Overview

The game state system needs to manage:
- Player state (position, movement flags, abilities)
- World state (time cycle, zones)
- Game rules and progression
- Input handling

## Implementation Plan

### 1. Core Game Framework Classes

#### Game Mode Blueprint (BP_CatGameMode)

1. Create a new Blueprint class:
   ```
   Content Browser -> Add New -> Blueprint Class -> Game Mode Base
   ```
   Name it `BP_CatGameMode`

2. Configure default settings:
   - Set Default Pawn Class to our `BP_CatCharacter`
   - Set Player Controller Class to our `BP_CatPlayerController`
   - Set Game State Class to our `BP_CatGameState`
   - Set Player State Class to our `BP_CatPlayerState`
   - Set HUD Class to our `BP_CatHUD`

3. Implement game rules in Event Graph:
   - Game start/end conditions
   - Scoring system
   - Respawn logic

#### Game State Blueprint (BP_CatGameState)

1. Create a new Blueprint class:
   ```
   Content Browser -> Add New -> Blueprint Class -> Game State Base
   ```
   Name it `BP_CatGameState`

2. Add variables for world state:
   - `DayCycle`: Struct containing:
     - `CurrentTime` (Float, range 0-1)
     - `IsNight` (Boolean)
     - `DayCycleMinutes` (Float, default 120)
     - `NightCycleMinutes` (Float, default 40)
   - `World`: Struct containing world data
   - `CurrentZones`: Array of Zone structs

3. Implement day/night cycle:
   - Add a timer in Event Begin Play
   - Update CurrentTime based on real time and cycle settings
   - Toggle IsNight based on CurrentTime threshold
   - Broadcast state changes to clients

4. Create functions for zone management:
   - `GetZoneAtLocation`: Returns zone data for a given world position
   - `UpdateZones`: Updates zone properties
   - `AddZone`: Creates a new zone
   - `RemoveZone`: Deletes a zone

#### Player State Blueprint (BP_CatPlayerState)

1. Create a new Blueprint class:
   ```
   Content Browser -> Add New -> Blueprint Class -> Player State
   ```
   Name it `BP_CatPlayerState`

2. Add variables for player state:
   - `CatData`: Struct containing cat properties:
     - `Name` (String)
     - `Level` (Integer)
     - `Health` (Float)
     - `Energy` (Float)
     - `Equipment` (Struct of equipped items)
   - `IsMoving` (Boolean)
   - `IsRunning` (Boolean)
   - `IsJumping` (Boolean)
   - `IsAttacking` (Boolean)
   - `IsDefending` (Boolean)
   - `CurrentZone`: Reference to zone object

3. Make variables replicate to ensure network synchronization
   - Select variables and enable "Replicated" in Details panel

4. Override GetLifetimeReplicatedProps function for replication

5. Create functions for state management:
   - `UpdateMovementState`: Sets movement flags
   - `UpdateCatData`: Updates cat stats and equipment
   - `TakeDamage`: Handles damage application
   - `HealDamage`: Handles healing
   - `UpdateEquipment`: Manages equipment changes

#### Player Controller Blueprint (BP_CatPlayerController)

1. Create a new Blueprint class:
   ```
   Content Browser -> Add New -> Blueprint Class -> Player Controller
   ```
   Name it `BP_CatPlayerController`

2. Set up input mappings:
   - Map keyboard keys (W, A, S, D, Space, Shift) to movement functions
   - Map mouse buttons to attack/defend functions
   - Map E key to interaction function
   - Map Tab key to inventory function

3. Create Event Graph logic for player input:
   - Convert raw input to movement commands
   - Handle action inputs (jump, attack, defend)
   - Update PlayerState with current action states

4. Implement camera controls:
   - Set up spring arm and camera components
   - Implement mouse look control
   - Add zoom functionality

### 2. Data Structures

Create the following Structures in the Unreal Editor:

#### Position Struct

1. Create a new Structure:
   ```
   Content Browser -> Add New -> Blueprint -> Structure
   ```
   Name it `S_Position`

2. Add the following variables:
   - `X` (Float)
   - `Y` (Float)
   - `Z` (Float)
   - `Rotation` (Float)

#### Zone Struct

1. Create a new Structure named `S_Zone`

2. Add the following variables:
   - `Name` (String)
   - `Type` (String)
   - `IsPvP` (Boolean)
   - `Bounds` (Struct with MinX, MaxX, MinY, MaxY, MinZ, MaxZ as Floats)
   - `SpecialProperties` (Array of Name-Value pairs)

#### Cat Struct

1. Create a new Structure named `S_Cat`

2. Add the following variables:
   - `Name` (String)
   - `Level` (Integer)
   - `Health` (Float)
   - `MaxHealth` (Float)
   - `Energy` (Float)
   - `MaxEnergy` (Float)
   - `Strength` (Float)
   - `Agility` (Float)
   - `Intelligence` (Float)
   - `Equipment` (Map of slot name to equipment item reference)

### 3. Blueprint Function Libraries

Create utility functions that can be called from any blueprint:

#### Game State Function Library

1. Create a new Blueprint Function Library:
   ```
   Content Browser -> Add New -> Blueprint Class -> Blueprint Function Library
   ```
   Name it `BFL_GameState`

2. Add static functions:
   - `GetCurrentDayTime`: Returns the current day/night cycle time
   - `IsNightTime`: Returns whether it's currently night
   - `GetZoneAtLocation`: Finds which zone contains a given position
   - `CalculateDamage`: Damage calculation helper

#### Player Function Library

1. Create a new Blueprint Function Library named `BFL_Player`

2. Add static functions:
   - `CanPlayerAttack`: Checks if player can perform attack
   - `CalculateMovementSpeed`: Returns speed based on state
   - `GetEquippedItemBonus`: Calculates stat bonus from equipment

### 4. Interface for Game State Updates

1. Create a new Interface:
   ```
   Content Browser -> Add New -> Blueprint Class -> Interface
   ```
   Name it `BPI_GameStateListener`

2. Add interface functions:
   - `OnDayCycleUpdated`: Called when day/night cycle changes
   - `OnZoneEntered`: Called when player enters a new zone
   - `OnZoneExited`: Called when player leaves a zone

3. Implement this interface in relevant blueprints:
   - `BP_CatCharacter`
   - `BP_ForestEnvironment`
   - `BP_CatHUD`

### 5. Editor Mode Implementation

For the editor functionality (terrain editing, zone creation, etc.):

1. Create a new Game Mode specifically for editing:
   ```
   Content Browser -> Add New -> Blueprint Class -> Game Mode Base
   ```
   Name it `BP_EditorGameMode`

2. Create editor tool blueprints:
   - `BP_TerrainEditTool`
   - `BP_ZoneEditTool`
   - `BP_ItemPlacementTool`
   - `BP_CatPlacementTool`

3. Create an editor controller:
   ```
   Content Browser -> Add New -> Blueprint Class -> Player Controller
   ```
   Name it `BP_EditorPlayerController`
   
   Implement functions for:
   - Switching between tools
   - Handling editor-specific input
   - Saving and loading edits

## Implementation Details

### Player Character Blueprint

The `BP_CatCharacter` class needs to handle:

1. **Character Movement Component Setup**:
   - Set walk speed to 500
   - Set run speed to 900
   - Configure jump parameters

2. **Animation Blueprint Integration**:
   - Create state machine with states for Idle, Walking, Running, Jumping, Attacking, Defending
   - Add blend spaces for smooth transitions
   - Connect movement flags from Player State to animation states

3. **Interaction with Game State**:
   - Listen for day/night cycle changes
   - Update movement flags in Player State
   - Handle zone transitions

Sample code for updating Player State from Character:

```
// In BP_CatCharacter Event Graph

// When movement input changes
Event MoveForward (Axis Value)
{
    // Call parent implementation to move character
    Call Parent: MoveForward
    
    // Update Player State
    Get Player State -> Cast to BP_CatPlayerState
    If (Cast Succeeded)
    {
        If (Axis Value != 0)
        {
            CatPlayerState.SetIsMoving(true)
        }
        Else If (Get Axis Value for MoveRight == 0)
        {
            CatPlayerState.SetIsMoving(false)
        }
    }
}

// When run input changes
Event Run (Press/Release)
{
    // Get player state and update running flag
    Get Player State -> Cast to BP_CatPlayerState
    If (Cast Succeeded)
    {
        If (Press)
        {
            CatPlayerState.SetIsRunning(true)
        }
        Else
        {
            CatPlayerState.SetIsRunning(false)
        }
    }
}
```

### Day/Night Cycle Implementation

The day/night cycle affects lighting, enemy behavior, and zone properties:

```
// In BP_CatGameState Event Graph

Event BeginPlay
{
    // Start day/night cycle timer
    Set Timer by Function Name (UpdateDayCycle, 1.0, true)
}

Function UpdateDayCycle
{
    // Calculate time progression
    CurrentTime = (CurrentTime + (1.0 / (60.0 * (IsNight ? NightCycleMinutes : DayCycleMinutes)))) % 1.0
    
    // Check for day/night transition
    If (CurrentTime >= 0.75 || CurrentTime < 0.25)
    {
        If (!IsNight)
        {
            IsNight = true
            BroadcastNightStarted()
        }
    }
    Else
    {
        If (IsNight)
        {
            IsNight = false
            BroadcastDayStarted()
        }
    }
    
    // Update world lighting based on time
    UpdateWorldLighting(CurrentTime)
    
    // Notify all listeners
    BroadcastDayCycleUpdated()
}
```

### Zone Management

Zones define areas with special properties:

```
// In BP_CatGameState

Function GetZoneAtLocation(Vector Location) -> S_Zone
{
    ForEach (Zone in CurrentZones)
    {
        If (Location.X >= Zone.Bounds.MinX && 
            Location.X <= Zone.Bounds.MaxX &&
            Location.Y >= Zone.Bounds.MinY && 
            Location.Y <= Zone.Bounds.MaxY &&
            Location.Z >= Zone.Bounds.MinZ && 
            Location.Z <= Zone.Bounds.MaxZ)
        {
            Return Zone
        }
    }
    
    Return EmptyZone // Default zone
}

// In BP_CatCharacter Event Graph

Event Tick
{
    // Check if player entered new zone
    S_Zone NewZone = BP_CatGameState.GetZoneAtLocation(GetActorLocation())
    
    Get Player State -> Cast to BP_CatPlayerState
    If (Cast Succeeded)
    {
        // If zone changed
        If (CatPlayerState.CurrentZone.Name != NewZone.Name)
        {
            // Handle zone transition
            CatPlayerState.SetCurrentZone(NewZone)
            
            // Notify listeners
            BP_CatGameState.BroadcastZoneChanged(NewZone)
        }
    }
}
```

## Connection to UI

1. Create Widget Blueprints in UMG:
   - `WBP_PlayerHUD` for displaying player stats
   - `WBP_Minimap` for navigation
   - `WBP_Inventory` for equipment management

2. Connect game state to UI through the HUD class:
   ```
   // In BP_CatHUD
   
   Event Tick
   {
       // Update HUD elements with current game state
       PlayerWidget.UpdateHealth(PlayerState.Health)
       PlayerWidget.UpdateEnergy(PlayerState.Energy)
       PlayerWidget.UpdateZone(PlayerState.CurrentZone.Name)
       
       // Update day/night indicator
       DayNightWidget.UpdateTimeOfDay(GameState.CurrentTime)
   }
   ```

## Saving/Loading Game State

Implement save/load functionality:

```
// In BP_CatGameMode

Function SaveGame
{
    // Create save game object
    SaveGameObj = Create Save Game Object (CatSaveGame)
    
    // Populate save data
    SaveGameObj.PlayerPosition = PlayerCharacter.GetActorLocation()
    SaveGameObj.PlayerRotation = PlayerCharacter.GetActorRotation()
    SaveGameObj.CatData = PlayerState.CatData
    SaveGameObj.WorldTime = GameState.CurrentTime
    SaveGameObj.IsNight = GameState.IsNight
    
    // Save to slot
    Save Game to Slot (SaveGameObj, "SaveSlot1")
}

Function LoadGame
{
    // Load save game object
    If (Does Save Game Exist ("SaveSlot1"))
    {
        SaveGameObj = Load Game from Slot ("SaveSlot1")
        
        // Restore player state
        PlayerCharacter.SetActorLocation(SaveGameObj.PlayerPosition)
        PlayerCharacter.SetActorRotation(SaveGameObj.PlayerRotation)
        PlayerState.CatData = SaveGameObj.CatData
        
        // Restore world state
        GameState.CurrentTime = SaveGameObj.WorldTime
        GameState.IsNight = SaveGameObj.IsNight
        GameState.UpdateWorldLighting(SaveGameObj.WorldTime)
    }
}
```