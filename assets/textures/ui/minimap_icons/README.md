# Minimap Icons

This directory contains icons for markers displayed on the minimap.

## Required Icons

1. **player.png**
   - Design: Triangle or arrow pointing up
   - Color: White or player color
   - Should rotate with player direction
   - Size: 24x24 pixels

2. **enemy.png**
   - Design: Red dot, skull, or enemy symbol
   - Color: Red (#F44336)
   - Should be easily distinguishable from other markers
   - Size: 24x24 pixels

3. **npc.png**
   - Design: Green person icon or friendly marker
   - Color: Green (#4CAF50)
   - Should indicate non-hostile characters
   - Size: 24x24 pixels

4. **quest.png**
   - Design: Yellow exclamation mark or star
   - Color: Gold (#FFD700)
   - Should be highly visible
   - Size: 24x24 pixels

5. **teammate.png**
   - Design: Blue dot or player icon
   - Color: Blue (#2196F3)
   - For multiplayer teammates
   - Size: 24x24 pixels

## Design Guidelines

- Icons must be clearly visible at small sizes
- Use high contrast colors
- Simple, iconic shapes work best
- Include subtle border or glow for visibility
- Consider using simple dots for many markers (performance)
- Format: PNG with alpha channel
- Minimap icons should be smaller than other UI icons (24x24 or 32x32)

## Optional Icons

You may also want to create:
- `waypoint.png` - Custom waypoint marker
- `resource.png` - Collectible resource marker
- `danger.png` - Danger zone marker
- `poi.png` - Point of interest marker
