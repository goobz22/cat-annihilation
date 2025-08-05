import React, { createContext, useContext, useState, ReactNode } from 'react';
import { CatCustomization } from '../components/game/CatCharacter/CustomizableCatMesh';

interface CatCustomizationContextType {
  playerCustomization: CatCustomization | null;
  setPlayerCustomization: (customization: CatCustomization) => void;
  npcCustomizations: Map<string, CatCustomization>;
  setNpcCustomization: (npcId: string, customization: CatCustomization) => void;
}

const CatCustomizationContext = createContext<CatCustomizationContextType | undefined>(undefined);

export const useCatCustomization = () => {
  const context = useContext(CatCustomizationContext);
  if (!context) {
    throw new Error('useCatCustomization must be used within a CatCustomizationProvider');
  }
  return context;
};

interface CatCustomizationProviderProps {
  children: ReactNode;
}

export const CatCustomizationProvider: React.FC<CatCustomizationProviderProps> = ({ children }) => {
  const [playerCustomization, setPlayerCustomization] = useState<CatCustomization | null>(null);
  const [npcCustomizations] = useState<Map<string, CatCustomization>>(new Map());

  const setNpcCustomization = (npcId: string, customization: CatCustomization) => {
    npcCustomizations.set(npcId, customization);
  };

  return (
    <CatCustomizationContext.Provider 
      value={{ 
        playerCustomization, 
        setPlayerCustomization, 
        npcCustomizations, 
        setNpcCustomization 
      }}
    >
      {children}
    </CatCustomizationContext.Provider>
  );
};