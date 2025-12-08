import * as THREE from 'three';

export type BiomeType = 'mist' | 'storm' | 'ember' | 'frost' | 'gathering' | 'neutral';

export interface BiomeProps {
  playerPosition: THREE.Vector3;
}

export interface TerrainFeature {
  position: [number, number, number];
  rotation?: [number, number, number];
  scale?: number;
  type?: string;
}

export interface BiomeConfig {
  fogColor: string;
  fogNear: number;
  fogFar: number;
  groundColor: string;
  groundTexture?: THREE.Texture;
  ambientLightIntensity: number;
  features: {
    trees: TerrainFeature[];
    rocks: TerrainFeature[];
    water?: TerrainFeature[];
    special?: TerrainFeature[];
  };
}