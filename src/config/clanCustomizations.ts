import { CatCustomization } from '../components/game/CatCharacter/CustomizableCatMesh';

/**
 * Predefined cat customizations for different clans and NPCs
 */

// Base customizations for each clan
export const CLAN_BASE_CUSTOMIZATIONS: Record<string, Partial<CatCustomization>> = {
  MistClan: {
    primaryColor: '#4A5568', // Gray-blue
    secondaryColor: '#718096',
    eyeColor: '#63B3ED', // Light blue
    pattern: 'tabby',
    patternColor: '#2D3748',
    furLength: 'medium',
  },
  StormClan: {
    primaryColor: '#2D3748', // Dark gray
    secondaryColor: '#1A202C',
    eyeColor: '#F6E05E', // Yellow
    pattern: 'solid',
    furLength: 'long',
    bodyType: 'normal',
  },
  EmberClan: {
    primaryColor: '#DD6B20', // Orange
    secondaryColor: '#ED8936',
    eyeColor: '#48BB78', // Green
    pattern: 'tabby',
    patternColor: '#C05621',
    furLength: 'medium',
  },
  FrostClan: {
    primaryColor: '#E2E8F0', // Light gray/white
    secondaryColor: '#CBD5E0',
    eyeColor: '#4299E1', // Ice blue
    pattern: 'solid',
    furLength: 'long',
    bodyType: 'normal',
  }
};

// Specific NPC customizations
export const NPC_CUSTOMIZATIONS: Record<string, CatCustomization> = {
  // MistClan NPCs
  'mistclan-leader': {
    ...CLAN_BASE_CUSTOMIZATIONS.MistClan,
    primaryColor: '#2C5282',
    eyeColor: '#90CDF4',
    noseColor: '#1A365D',
    earSize: 'large',
    tailLength: 'long',
    bodyType: 'normal',
    collar: {
      color: '#3182CE',
      hasTag: true
    },
    scars: [{ position: 'ear', side: 'left' }]
  } as CatCustomization,
  
  'mistclan-mentor': {
    ...CLAN_BASE_CUSTOMIZATIONS.MistClan,
    primaryColor: '#4A5568',
    eyeColor: '#4FD1C5',
    noseColor: '#2D3748',
    pattern: 'tabby',
    patternColor: '#1A202C',
    furLength: 'medium',
    tailLength: 'normal'
  } as CatCustomization,
  
  'mistclan-elder': {
    ...CLAN_BASE_CUSTOMIZATIONS.MistClan,
    primaryColor: '#CBD5E0',
    eyeColor: '#718096',
    noseColor: '#4A5568',
    pattern: 'solid',
    furLength: 'long',
    earSize: 'small',
    scars: [
      { position: 'eye', side: 'right' },
      { position: 'ear', side: 'left' }
    ]
  } as CatCustomization,
  
  // StormClan NPCs
  'stormclan-leader': {
    ...CLAN_BASE_CUSTOMIZATIONS.StormClan,
    primaryColor: '#1A202C',
    eyeColor: '#FBD38D',
    noseColor: '#2D3748',
    bodyType: 'normal',
    earSize: 'large',
    tailLength: 'long',
    collar: {
      color: '#744210',
      hasTag: true
    }
  } as CatCustomization,
  
  'stormclan-warrior': {
    ...CLAN_BASE_CUSTOMIZATIONS.StormClan,
    primaryColor: '#2D3748',
    eyeColor: '#F6E05E',
    noseColor: '#1A202C',
    pattern: 'spots',
    patternColor: '#000000',
    bodyType: 'normal',
    scars: [{ position: 'body' }]
  } as CatCustomization,
  
  // EmberClan NPCs
  'emberclan-leader': {
    ...CLAN_BASE_CUSTOMIZATIONS.EmberClan,
    primaryColor: '#C05621',
    eyeColor: '#68D391',
    noseColor: '#7C2D12',
    pattern: 'tabby',
    patternColor: '#7C2D12',
    bodyType: 'normal',
    earSize: 'normal',
    tailLength: 'long',
    collar: {
      color: '#DC2626',
      hasTag: true
    }
  } as CatCustomization,
  
  'emberclan-elder': {
    ...CLAN_BASE_CUSTOMIZATIONS.EmberClan,
    primaryColor: '#FED7AA',
    eyeColor: '#9CA3AF',
    noseColor: '#DC2626',
    pattern: 'calico',
    patternColor: '#FFF',
    secondaryColor: '#000',
    furLength: 'long',
    bodyType: 'chubby'
  } as CatCustomization,
  
  // FrostClan NPCs
  'frostclan-leader': {
    ...CLAN_BASE_CUSTOMIZATIONS.FrostClan,
    primaryColor: '#F7FAFC',
    eyeColor: '#2B6CB0',
    noseColor: '#E53E3E',
    pattern: 'solid',
    furLength: 'long',
    bodyType: 'normal',
    earSize: 'normal',
    tailLength: 'long',
    collar: {
      color: '#60A5FA',
      hasTag: true
    }
  } as CatCustomization,
  
  'frostclan-apprentice': {
    ...CLAN_BASE_CUSTOMIZATIONS.FrostClan,
    primaryColor: '#E0E7FF',
    eyeColor: '#6366F1',
    noseColor: '#EC4899',
    pattern: 'tuxedo',
    patternColor: '#FFFFFF',
    furLength: 'short',
    bodyType: 'slim',
    earSize: 'large',
    tailLength: 'normal'
  } as CatCustomization
};

// Generate random customization for a clan
export function generateRandomClanCat(clan: string): CatCustomization {
  const baseCustom = CLAN_BASE_CUSTOMIZATIONS[clan] || CLAN_BASE_CUSTOMIZATIONS.MistClan;
  
  const patterns: Array<CatCustomization['pattern']> = ['solid', 'tabby', 'spots', 'tuxedo'];
  const eyeColors = ['#48BB78', '#4299E1', '#F6E05E', '#EC4899', '#9F7AEA', '#ED8936'];
  const earSizes: Array<CatCustomization['earSize']> = ['small', 'normal', 'large'];
  const tailLengths: Array<CatCustomization['tailLength']> = ['short', 'normal', 'long'];
  const bodyTypes: Array<CatCustomization['bodyType']> = ['slim', 'normal', 'chubby'];
  
  return {
    ...baseCustom,
    primaryColor: baseCustom.primaryColor!,
    eyeColor: eyeColors[Math.floor(Math.random() * eyeColors.length)],
    noseColor: '#EC4899',
    pattern: patterns[Math.floor(Math.random() * patterns.length)],
    earSize: earSizes[Math.floor(Math.random() * earSizes.length)],
    tailLength: tailLengths[Math.floor(Math.random() * tailLengths.length)],
    bodyType: bodyTypes[Math.floor(Math.random() * bodyTypes.length)],
    furLength: baseCustom.furLength || 'medium'
  };
}

// Player starter customizations based on clan choice
export function getPlayerStarterCustomization(clan: string): CatCustomization {
  const baseCustom = CLAN_BASE_CUSTOMIZATIONS[clan] || CLAN_BASE_CUSTOMIZATIONS.MistClan;
  
  return {
    primaryColor: baseCustom.primaryColor!,
    secondaryColor: baseCustom.secondaryColor,
    eyeColor: '#48BB78', // Green eyes for player
    noseColor: '#EC4899',
    pattern: 'solid',
    patternColor: baseCustom.patternColor,
    earSize: 'normal',
    tailLength: 'normal',
    furLength: 'medium',
    bodyType: 'normal'
  };
}