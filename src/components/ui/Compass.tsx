import React, { useEffect, useRef, useState } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

interface CompassMarker {
  id: string;
  type: 'biome' | 'quest' | 'npc' | 'landmark';
  name: string;
  position: { x: number; z: number };
  color: string;
  icon?: string;
  distance?: number;
}

const Compass: React.FC = () => {
  const playerPosition = useGameStore(state => state.player.position);
  const playerRotation = useGameStore(state => state.player.position.rotation);
  const storyMode = useGameStore(state => state.storyMode);
  const quests = storyMode.quests;
  const activeQuestIds = storyMode.activeQuests;
  const currentBiome = storyMode.currentBiome;
  
  const compassRef = useRef<HTMLDivElement>(null);
  const [markers, setMarkers] = useState<CompassMarker[]>([]);

  // Define biome centers
  const biomeMarkers: CompassMarker[] = [
    { id: 'mist', type: 'biome', name: 'MistClan', position: { x: -150, z: -150 }, color: '#4A90E2', icon: '🌊' },
    { id: 'storm', type: 'biome', name: 'StormClan', position: { x: 150, z: -150 }, color: '#5A5A5A', icon: '⚡' },
    { id: 'ember', type: 'biome', name: 'EmberClan', position: { x: 150, z: 150 }, color: '#D2691E', icon: '🍂' },
    { id: 'frost', type: 'biome', name: 'FrostClan', position: { x: -150, z: 150 }, color: '#87CEEB', icon: '❄️' },
    { id: 'gathering', type: 'landmark', name: 'Gathering Place', position: { x: 0, z: 0 }, color: '#FFD700', icon: '🌟' },
  ];

  // Update markers based on active quests and biomes
  useEffect(() => {
    const allMarkers = [...biomeMarkers];

    // Add quest markers for active quests
    const activeQuests = quests.filter(q => activeQuestIds?.includes(q.id));
    activeQuests.forEach(quest => {
      if (quest.objectives) {
        quest.objectives.forEach(objective => {
          // For now, place quest objectives at clan locations
          // In a full implementation, each objective would have specific coordinates
          let questLocation = { x: 0, z: 0 };
          
          if (quest.location === 'MistClan') {
            questLocation = { x: -150, z: -150 };
          } else if (quest.location === 'StormClan') {
            questLocation = { x: 150, z: -150 };
          } else if (quest.location === 'EmberClan') {
            questLocation = { x: 150, z: 150 };
          } else if (quest.location === 'FrostClan') {
            questLocation = { x: -150, z: 150 };
          }
          
          if (objective.currentCount < objective.count) {
            allMarkers.push({
              id: `quest-${quest.id}-${objective.id}`,
              type: 'quest',
              name: objective.description,
              position: questLocation,
              color: '#FFD700',
              icon: '!'
            });
          }
        });
      }
    });

    // Calculate distances
    const markersWithDistance = allMarkers.map(marker => {
      const dx = marker.position.x - playerPosition.x;
      const dz = marker.position.z - playerPosition.z;
      const distance = Math.sqrt(dx * dx + dz * dz);
      return { ...marker, distance: Math.round(distance) };
    });

    setMarkers(markersWithDistance);
  }, [playerPosition, quests, activeQuestIds]);

  // Calculate angle to marker relative to player
  const getMarkerAngle = (marker: CompassMarker) => {
    const dx = marker.position.x - playerPosition.x;
    const dz = marker.position.z - playerPosition.z;
    let angle = Math.atan2(dx, -dz); // Negative Z because Three.js Z is inverted
    
    // Adjust for player rotation
    angle -= playerRotation;
    
    // Normalize to 0-360 degrees
    angle = angle * (180 / Math.PI);
    while (angle < 0) angle += 360;
    while (angle >= 360) angle -= 360;
    
    return angle;
  };

  // Check if marker is visible on compass (within 45 degrees of center)
  const isMarkerVisible = (angle: number) => {
    // Normalize angle to -180 to 180 range
    if (angle > 180) angle -= 360;
    return Math.abs(angle) <= 90; // Show markers within 90 degrees
  };

  // Get position on compass bar (0-100%)
  const getMarkerPosition = (angle: number) => {
    // Normalize angle to -180 to 180 range
    if (angle > 180) angle -= 360;
    
    // Map -90 to 90 degrees to 0-100% position
    const position = ((angle + 90) / 180) * 100;
    return Math.max(0, Math.min(100, position));
  };

  // Get cardinal direction based on angle
  const getCardinalDirection = (angle: number) => {
    if (angle >= 337.5 || angle < 22.5) return 'N';
    if (angle >= 22.5 && angle < 67.5) return 'NE';
    if (angle >= 67.5 && angle < 112.5) return 'E';
    if (angle >= 112.5 && angle < 157.5) return 'SE';
    if (angle >= 157.5 && angle < 202.5) return 'S';
    if (angle >= 202.5 && angle < 247.5) return 'SW';
    if (angle >= 247.5 && angle < 292.5) return 'W';
    return 'NW';
  };

  // Calculate compass rotation based on player rotation
  const compassRotation = playerRotation * (180 / Math.PI);

  return (
    <div className="compass-container">
      <div className="compass-bar" ref={compassRef}>
        {/* Compass background with degree markings */}
        <div className="compass-background">
          {/* Cardinal directions */}
          {['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'].map((dir, index) => {
            const angle = index * 45;
            const adjustedAngle = (angle - compassRotation + 360) % 360;
            
            if (isMarkerVisible(adjustedAngle)) {
              return (
                <div
                  key={dir}
                  className="compass-direction"
                  style={{ left: `${getMarkerPosition(adjustedAngle)}%` }}
                >
                  <span className={dir === 'N' || dir === 'S' || dir === 'E' || dir === 'W' ? 'major' : 'minor'}>
                    {dir}
                  </span>
                </div>
              );
            }
            return null;
          })}

          {/* Degree marks every 15 degrees */}
          {Array.from({ length: 24 }, (_, i) => i * 15).map(degree => {
            const adjustedAngle = (degree - compassRotation + 360) % 360;
            
            if (isMarkerVisible(adjustedAngle) && degree % 45 !== 0) {
              return (
                <div
                  key={`degree-${degree}`}
                  className="compass-tick"
                  style={{ left: `${getMarkerPosition(adjustedAngle)}%` }}
                />
              );
            }
            return null;
          })}
        </div>

        {/* Markers */}
        {markers.map(marker => {
          const angle = getMarkerAngle(marker);
          
          if (isMarkerVisible(angle)) {
            return (
              <div
                key={marker.id}
                className={`compass-marker ${marker.type}`}
                style={{
                  left: `${getMarkerPosition(angle)}%`,
                  color: marker.color
                }}
              >
                <div className="marker-icon">{marker.icon}</div>
                <div className="marker-label">
                  <span className="marker-name">{marker.name}</span>
                  <span className="marker-distance">{marker.distance}m</span>
                </div>
              </div>
            );
          }
          return null;
        })}

        {/* Center indicator */}
        <div className="compass-center-indicator" />
      </div>

      {/* Current biome display */}
      <div className="compass-biome-display">
        <span className="biome-label">Current Territory:</span>
        <span className="biome-name" style={{ color: getBiomeColor(currentBiome) }}>
          {getBiomeName(currentBiome)}
        </span>
      </div>
    </div>
  );
};

// Helper functions
const getBiomeColor = (biome?: string): string => {
  switch (biome) {
    case 'mist': return '#4A90E2';
    case 'storm': return '#5A5A5A';
    case 'ember': return '#D2691E';
    case 'frost': return '#87CEEB';
    case 'gathering': return '#FFD700';
    default: return '#FFFFFF';
  }
};

const getBiomeName = (biome?: string): string => {
  switch (biome) {
    case 'mist': return 'MistClan Territory';
    case 'storm': return 'StormClan Territory';
    case 'ember': return 'EmberClan Territory';
    case 'frost': return 'FrostClan Territory';
    case 'gathering': return 'The Gathering Place';
    case 'neutral': return 'Neutral Territory';
    default: return 'Unknown Territory';
  }
};

export default Compass;