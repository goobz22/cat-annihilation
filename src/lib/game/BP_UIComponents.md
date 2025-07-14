# UI Components Implementation in Unreal Engine UMG

This document outlines how to implement the user interface components in Unreal Engine using the UMG (Unreal Motion Graphics) system, based on the React components from the original implementation.

## Overview

The game UI consists of:
- Player stats display (health, attack, defense, speed, level)
- Day/night cycle indicator
- Zone information
- Currency display
- Controls help panel
- Inventory system

## Implementation Plan

### 1. Creating the Main HUD Widget

1. Create a new Widget Blueprint:
   ```
   Content Browser -> Add New -> User Interface -> Widget Blueprint
   ```
   Name it `WBP_MainHUD`

2. Set up the main canvas panel:
   - In the Designer tab, add a Canvas Panel as the root
   - Configure it to fill the entire screen

3. Create UI sections:
   - Add an Overlay in the upper left for the main UI panel
   - Add background images and styling

### 2. Time Display Widget

1. Create a new Widget Blueprint named `WBP_TimeDisplay`

2. Design the widget in the Designer tab:
   - Add a Horizontal Box for layout
   - Add an Image for the day/night indicator
   - Add a Text Block for time display
   - Add a Text Block for "Day" or "Night" label

3. Add variables:
   - `CurrentTime` (Float, range 0-1)
   - `IsNight` (Boolean)

4. In the Graph tab, create a function to update the display:
   ```
   Function UpdateTimeDisplay(Float Time, Boolean Night)
   {
       // Store values
       CurrentTime = Time
       IsNight = Night
       
       // Convert time (0-1) to hours (0-24)
       Hours = Floor(CurrentTime * 24)
       Minutes = Floor((CurrentTime * 24 * 60) % 60)
       
       // Format time as HH:MM
       FormattedTime = Format Text("{0}:{1}", 
           Right(Append("0", ToString(Hours)), 2),
           Right(Append("0", ToString(Minutes)), 2))
       
       // Set text display
       TimeText.SetText(FormattedTime)
       DayNightText.SetText(IsNight ? "Night" : "Day")
       
       // Set indicator color
       DayNightImage.SetColorAndOpacity(IsNight ? (R=0.5, G=0.7, B=1.0, A=1.0) : (R=1.0, G=0.9, B=0.2, A=1.0))
   }
   ```

5. In the Event Construct, set default values:
   ```
   Event Construct
   {
       UpdateTimeDisplay(0.5, false)
   }
   ```

### 3. Zone Display Widget

1. Create a new Widget Blueprint named `WBP_ZoneDisplay`

2. Design the widget:
   - Add a Vertical Box
   - Add a Text Block for zone name
   - Add a Text Block for zone status (Safe/PvP)

3. Add variables:
   - `ZoneName` (String)
   - `IsPvP` (Boolean)

4. Create update function:
   ```
   Function UpdateZoneDisplay(String Name, Boolean PvP)
   {
       ZoneName = Name
       IsPvP = PvP
       
       // Update text
       ZoneNameText.SetText(ZoneName)
       
       // Update PvP indicator
       If (IsPvP)
       {
           ZoneStatusText.SetText("PvP Zone")
           ZoneStatusText.SetColorAndOpacity(Color=(R=1.0, G=0.3, B=0.3, A=1.0))
       }
       Else
       {
           ZoneStatusText.SetText("Safe Zone")
           ZoneStatusText.SetColorAndOpacity(Color=(R=0.3, G=1.0, B=0.3, A=1.0))
       }
   }
   ```

### 4. Stats Display Widget

1. Create a new Widget Blueprint named `WBP_StatsDisplay`

2. Design the widget:
   - Add a Vertical Box for layout
   - Add a Text Block for "Health" label
   - Add a Progress Bar for health visualization
   - Add Text Blocks for attack, defense, speed, and level stats

3. Add variables:
   - `CurrentHealth` (Float)
   - `MaxHealth` (Float)
   - `Attack` (Integer)
   - `Defense` (Integer)
   - `Speed` (Integer)
   - `Level` (Integer)

4. Create update function:
   ```
   Function UpdateStats(Float Health, Float MaxHP, Int AttackVal, Int DefenseVal, Int SpeedVal, Int LevelVal)
   {
       // Store values
       CurrentHealth = Health
       MaxHealth = MaxHP
       Attack = AttackVal
       Defense = DefenseVal
       Speed = SpeedVal
       Level = LevelVal
       
       // Update health bar
       HealthBar.SetPercent(CurrentHealth / MaxHealth)
       
       // Change color based on health percentage
       If (CurrentHealth / MaxHealth < 0.3)
       {
           // Low health - red
           HealthBar.SetFillColorAndOpacity(Color=(R=1.0, G=0.0, B=0.0, A=1.0))
       }
       Else If (CurrentHealth / MaxHealth < 0.6)
       {
           // Medium health - yellow
           HealthBar.SetFillColorAndOpacity(Color=(R=1.0, G=0.8, B=0.0, A=1.0))
       }
       Else
       {
           // High health - green
           HealthBar.SetFillColorAndOpacity(Color=(R=0.0, G=0.8, B=0.0, A=1.0))
       }
       
       // Update stat text
       AttackText.SetText(Format Text("Attack: {0}", Attack))
       DefenseText.SetText(Format Text("Defense: {0}", Defense))
       SpeedText.SetText(Format Text("Speed: {0}", Speed))
       LevelText.SetText(Format Text("Level: {0}", Level))
   }
   ```

### 5. Currency Display Widget

1. Create a new Widget Blueprint named `WBP_CurrencyDisplay`

2. Design the widget:
   - Add a Horizontal Box
   - Add a Text Block for currency amount
   - Add a Text Block for "Coins" label

3. Add variable:
   - `Currency` (Integer)

4. Create update function:
   ```
   Function UpdateCurrency(Int Amount)
   {
       Currency = Amount
       CurrencyText.SetText(ToString(Currency))
   }
   ```

### 6. Controls Help Panel

1. Create a new Widget Blueprint named `WBP_ControlsHelp`

2. Design the widget with two states:
   - Collapsed state: Just a "Controls" button
   - Expanded state: A panel with control information

3. Add a Button for toggling visibility
4. Add a Border with control information inside
5. Add a Grid Panel for the control mappings:
   - Left column: Key names (W, S, A, D, Space)
   - Right column: Action descriptions

6. Create toggle function:
   ```
   Event OnControlsButtonClicked
   {
       // Toggle visibility of controls panel
       If (ControlsPanel.Visibility == Hidden)
       {
           ControlsPanel.SetVisibility(Visible)
       }
       Else
       {
           ControlsPanel.SetVisibility(Hidden)
       }
   }
   ```

7. Bind the button's OnClicked event to this function

### 7. Inventory System

1. Create a new Widget Blueprint named `WBP_Inventory`

2. Design the inventory:
   - Add a Canvas Panel for the background
   - Add a Border for the window frame
   - Add a Uniform Grid Panel for item slots
   - Add a Button for closing the inventory

3. Create item slot components:
   - Create a Widget Blueprint named `WBP_InventorySlot`
   - Add an Image for the item icon
   - Add a Text Block for item count
   - Add a Button for the clickable area
   - Add tooltip functionality

4. In the main inventory widget, populate slots:
   ```
   Function PopulateInventory(Array<ItemData> Items)
   {
       // Clear existing slots
       ItemGrid.ClearChildren()
       
       // Add slots for each item
       ForEach (ItemData in Items)
       {
           // Create new inventory slot
           Slot = Create Widget(WBP_InventorySlot)
           
           // Set slot data
           Slot.SetItemData(ItemData)
           
           // Add to grid
           ItemGrid.AddChild(Slot)
       }
   }
   ```

5. Add drag and drop functionality:
   - Implement OnDragDetected in item slots
   - Create a drag visual
   - Implement OnDrop to handle item movements

### 8. Editor Interface (if needed)

1. Create a new Widget Blueprint named `WBP_EditorPanel`

2. Design the editor panel:
   - Add a Vertical Box for layout
   - Add a Text Block for the "Editor Tools" header
   - Add Buttons for each tool (Terrain, Zones, Items, Cats)

3. Create tool selection function:
   ```
   Function SelectTool(String ToolName)
   {
       // Update button states
       TerrainButton.SetIsEnabled(ToolName != "terrain")
       ZonesButton.SetIsEnabled(ToolName != "zones")
       ItemsButton.SetIsEnabled(ToolName != "items")
       CatsButton.SetIsEnabled(ToolName != "cats")
       
       // Update button appearance
       TerrainButton.SetBackgroundColor(ToolName == "terrain" ? SelectedColor : NormalColor)
       ZonesButton.SetBackgroundColor(ToolName == "zones" ? SelectedColor : NormalColor)
       ItemsButton.SetBackgroundColor(ToolName == "items" ? SelectedColor : NormalColor)
       CatsButton.SetBackgroundColor(ToolName == "cats" ? SelectedColor : NormalColor)
       
       // Notify game mode about tool change
       GetOwningPlayer().SwitchEditorTool(ToolName)
   }
   ```

4. Bind button clicks to tool selection function

### 9. Connecting HUD to Game State

1. In the `BP_CatHUD` class:
   ```
   Event BeginPlay
   {
       // Create main HUD widget
       MainHUDWidget = Create Widget(WBP_MainHUD)
       
       // Add to viewport
       MainHUDWidget.AddToViewport()
   }
   
   Event Tick
   {
       // Get player state
       PlayerStateRef = GetOwningPlayerController().PlayerState as BP_CatPlayerState
       
       // Get game state
       GameStateRef = GetWorld().GetGameState() as BP_CatGameState
       
       // Only update if we have valid references
       If (IsValid(PlayerStateRef) && IsValid(GameStateRef))
       {
           // Update time display
           MainHUDWidget.TimeDisplay.UpdateTimeDisplay(
               GameStateRef.DayCycle.CurrentTime,
               GameStateRef.DayCycle.IsNight
           )
           
           // Update zone display
           MainHUDWidget.ZoneDisplay.UpdateZoneDisplay(
               PlayerStateRef.CurrentZone.Name,
               PlayerStateRef.CurrentZone.IsPvP
           )
           
           // Update stats display
           MainHUDWidget.StatsDisplay.UpdateStats(
               PlayerStateRef.CatData.Health,
               PlayerStateRef.CatData.MaxHealth,
               PlayerStateRef.CatData.Attack,
               PlayerStateRef.CatData.Defense,
               PlayerStateRef.CatData.Speed,
               PlayerStateRef.CatData.Level
           )
           
           // Update currency display
           MainHUDWidget.CurrencyDisplay.UpdateCurrency(
               PlayerStateRef.CatData.Currency
           )
       }
   }
   ```

2. Set up input handling for inventory:
   ```
   // In BP_CatPlayerController
   
   Event BeginPlay
   {
       // Set up input mapping for inventory
       EnableInput(GetLocalPlayer().GetPlayerController())
       
       // Bind inventory key
       InputComponent.BindAction("ToggleInventory", IE_Pressed, this, "ToggleInventory")
   }
   
   Function ToggleInventory
   {
       // Get HUD reference
       HUDRef = GetHUD() as BP_CatHUD
       
       If (IsValid(HUDRef))
       {
           // Toggle inventory visibility
           HUDRef.ToggleInventory()
       }
   }
   ```

## Styling and Themes

### UMG Style Assets

1. Create a Style asset for consistent UI appearance:
   ```
   Content Browser -> Add New -> User Interface -> Widget Style
   ```
   Name it `S_CatUIStyle`

2. Define common styles:
   - Text styles (headers, body text, etc.)
   - Button styles (normal, hover, pressed)
   - Background styles
   - Color schemes

3. Apply these styles to all UI widgets for consistency

### Animation Effects

1. Add animations in the Widget Designer:
   - Fade in/out for panels
   - Scale animations for buttons
   - Color transitions

2. Example for inventory panel:
   ```
   // In WBP_Inventory
   
   Function Show
   {
       // Play fade in animation
       PlayAnimation(FadeIn)
       
       // Make widget interactive
       SetVisibility(Visible)
   }
   
   Function Hide
   {
       // Play fade out animation
       PlayAnimation(FadeOut)
       
       // After animation completes, hide the widget
       BindToAnimationFinished(FadeOut, HideAfterFade)
   }
   
   Function HideAfterFade
   {
       SetVisibility(Hidden)
   }
   ```

## Mobile Compatibility (Optional)

If targeting mobile platforms:

1. Add touch input support:
   - Virtual joystick for movement
   - Touch buttons for actions
   - Swipe gestures for camera control

2. Adjust UI scaling:
   - Set "Design for Mobile" in Widget Designer
   - Test on different screen sizes
   - Implement responsive layout

## Performance Considerations

1. Use widget caching:
   - Create widgets once and reuse them
   - Avoid frequent widget creation/destruction

2. Minimize Tick events:
   - Only update UI when values change
   - Use event-driven updates when possible

3. Optimize animations:
   - Keep animations simple
   - Limit the number of simultaneous animations

## Event-Driven Architecture

1. Create a UI event dispatcher in the Game Mode:
   ```
   // In BP_CatGameMode
   
   // Declare event dispatchers
   Event Dispatcher OnPlayerStatsChanged(Health, MaxHealth, Attack, Defense, Speed, Level)
   Event Dispatcher OnZoneChanged(ZoneName, IsPvP)
   Event Dispatcher OnCurrencyChanged(Amount)
   Event Dispatcher OnTimeChanged(CurrentTime, IsNight)
   ```

2. Bind UI updates to these events:
   ```
   // In BP_CatHUD
   
   Event BeginPlay
   {
       // Get game mode
       GameMode = GetWorld().GetAuthGameMode() as BP_CatGameMode
       
       // Bind to events
       GameMode.OnPlayerStatsChanged.AddDynamic(this, "UpdatePlayerStats")
       GameMode.OnZoneChanged.AddDynamic(this, "UpdateZoneDisplay")
       GameMode.OnCurrencyChanged.AddDynamic(this, "UpdateCurrency")
       GameMode.OnTimeChanged.AddDynamic(this, "UpdateTimeDisplay")
   }
   
   Function UpdatePlayerStats(Health, MaxHealth, Attack, Defense, Speed, Level)
   {
       MainHUDWidget.StatsDisplay.UpdateStats(Health, MaxHealth, Attack, Defense, Speed, Level)
   }
   
   // Similar functions for other updates
   ```

This event-driven approach will be more efficient than polling for changes in Tick events.