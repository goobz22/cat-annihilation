# UI Textures

This directory contains all UI textures for the Cat Annihilation game.

## Directory Structure

### `/quest_icons/`
Icons for different quest types:
- `main.png` - Main story quest icon
- `side.png` - Side quest icon
- `daily.png` - Daily quest icon
- `clan.png` - Clan mission icon
- `bounty.png` - Bounty quest icon
- `default.png` - Default quest icon

**Recommended size:** 64x64 pixels
**Format:** PNG with alpha channel

### `/spell_icons/`
Icons for all elemental spells (20 spells total):

**Water Spells:**
- `water_bolt.png`
- `healing_rain.png`
- `tidal_wave.png`
- `ice_prison.png`
- `tsunami.png`

**Air Spells:**
- `wind_gust.png`
- `haste.png`
- `lightning_bolt.png`
- `tornado.png`
- `storm_call.png`

**Earth Spells:**
- `rock_throw.png`
- `stone_skin.png`
- `earthquake.png`
- `wall_of_stone.png`
- `meteor_strike.png`

**Fire Spells:**
- `fireball.png`
- `flame_shield.png`
- `inferno.png`
- `phoenix_strike.png`
- `apocalypse.png`

**Recommended size:** 64x64 pixels
**Format:** PNG with alpha channel

### `/element_icons/`
Icons for the four elemental types:
- `water.png` - Water element (blue)
- `air.png` - Air element (white/cyan)
- `earth.png` - Earth element (brown/green)
- `fire.png` - Fire element (red/orange)

**Recommended size:** 64x64 pixels
**Format:** PNG with alpha channel

### `/minimap_icons/`
Icons for minimap markers:
- `player.png` - Player indicator (triangle/arrow)
- `enemy.png` - Enemy marker (red dot/skull)
- `npc.png` - NPC marker (green person icon)
- `quest.png` - Quest objective marker (yellow exclamation)
- `teammate.png` - Teammate marker (blue dot)

**Recommended size:** 24x24 or 32x32 pixels
**Format:** PNG with alpha channel

### `/item_icons/`
Icons for inventory items (named by item ID):
- Weapon icons
- Armor icons
- Consumable icons
- Material icons
- Quest item icons
- Miscellaneous item icons

**Recommended size:** 64x64 pixels
**Format:** PNG with alpha channel
**Naming:** Use item ID as filename (e.g., `sword_iron.png`, `potion_health.png`)

### `/category_icons/`
Icons for item categories:
- `weapon.png` - Weapon category
- `armor.png` - Armor category
- `consumable.png` - Consumable category
- `material.png` - Material category
- `quest.png` - Quest item category
- `misc.png` - Miscellaneous category

**Recommended size:** 32x32 pixels
**Format:** PNG with alpha channel

## Root UI Textures

The following textures should be placed directly in `/assets/textures/ui/`:

### Compass UI
- `compass_bg.png` - Compass background bar
- `compass_arrow.png` - Compass arrow/pointer

### Minimap UI
- `minimap_bg.png` - Minimap background circle/square
- `minimap_border.png` - Minimap border

### General UI
- `crosshair.png` - Crosshair for aiming
- `slot_bg.png` - Generic slot background
- `slot_selected.png` - Selected slot highlight
- `button_bg.png` - Generic button background
- `window_bg.png` - Generic window background

## Asset Guidelines

### General Requirements
- All UI textures should use PNG format with alpha channel
- Use consistent art style across all icons
- Ensure icons are clearly visible at small sizes
- Test icons against both light and dark backgrounds

### Color Coding
- **Quest Types:**
  - Main Story: Gold (#FFD700)
  - Side Quest: Silver (#C0C0C0)
  - Daily: Light Blue (#4FC3F7)
  - Clan Mission: Purple (#9C27B0)
  - Bounty: Red (#F44336)

- **Item Rarity:**
  - Common: Gray (#CCCCCC)
  - Uncommon: Green (#4CAF50)
  - Rare: Blue (#2196F3)
  - Epic: Purple (#9C27B0)
  - Legendary: Orange (#FF9800)

- **Elements:**
  - Water: Blue (#2196F3)
  - Air: White/Cyan (#E1F5FE)
  - Earth: Brown/Green (#795548)
  - Fire: Red/Orange (#FF5722)

### Performance Considerations
- Keep texture sizes reasonable (most icons should be 64x64 or smaller)
- Use texture atlases when possible to reduce draw calls
- Compress textures appropriately while maintaining quality
- Use mipmaps for textures that may be viewed at different sizes

## Placeholder Assets

For development purposes, simple colored squares or circles can be used as placeholders:
- Use the color codes above for easy identification
- Add text labels to placeholders for clarity
- Replace with final artwork before release
