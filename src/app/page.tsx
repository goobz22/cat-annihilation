'use client';

import { useState, useEffect } from 'react';
import { ApolloProvider } from '@apollo/client';
import { useApollo } from '@/lib/graphql/apollo-client';
import GameScene from '@/components/game/GameScene';
import GameInterface from '@/components/ui/GameInterface';
import GameProvider from '@/components/ui/GameProvider';

/**
 * Authentication state
 */
interface Auth {
  isAuthenticated: boolean;
  userId: string | null;
}

/**
 * Character selection component
 */
const CharacterSelection = ({ onSelect }: { onSelect: (catId: string) => void }) => {
  // In a real app, we'd fetch cats from the database
  const dummyCats = [
    { id: '1', name: 'Shadowpaw', level: 5, clan: 'ThunderClan' },
    { id: '2', name: 'Stormfur', level: 3, clan: 'RiverClan' },
    { id: '3', name: 'Leafwhisker', level: 7, clan: 'WindClan' },
  ];

  return (
    <div className="bg-gray-900 min-h-screen flex items-center justify-center">
      <div className="bg-gray-800 p-8 rounded-lg shadow-2xl max-w-md w-full">
        <h1 className="text-3xl font-bold text-white mb-6 text-center">Choose Your Cat</h1>
        
        <div className="space-y-4">
          {dummyCats.map((cat) => (
            <div 
              key={cat.id}
              className="bg-gray-700 hover:bg-gray-600 p-4 rounded-lg cursor-pointer"
              onClick={() => onSelect(cat.id)}
            >
              <div className="flex items-center justify-between">
                <div>
                  <h3 className="text-xl font-bold text-white">{cat.name}</h3>
                  <div className="text-gray-300 text-sm">Level {cat.level} • {cat.clan}</div>
                </div>
                <div className="w-12 h-12 bg-gray-600 rounded-full"></div>
              </div>
            </div>
          ))}
          
          <button className="w-full mt-6 bg-indigo-600 hover:bg-indigo-500 text-white font-bold py-2 px-4 rounded">
            Create New Cat
          </button>
        </div>
      </div>
    </div>
  );
};

/**
 * Login component
 */
const Login = ({ onLogin }: { onLogin: () => void }) => {
  return (
    <div className="bg-gray-900 min-h-screen flex items-center justify-center">
      <div className="bg-gray-800 p-8 rounded-lg shadow-2xl max-w-md w-full">
        <h1 className="text-3xl font-bold text-white mb-6 text-center">Cat Annihilation</h1>
        <p className="text-gray-300 mb-8 text-center">Enter the world of warrior cats</p>
        
        <div className="space-y-4">
          <div>
            <label className="block text-gray-300 mb-1">Username</label>
            <input 
              type="text" 
              className="w-full bg-gray-700 text-white border border-gray-600 rounded py-2 px-3 focus:outline-none focus:border-indigo-500"
            />
          </div>
          
          <div>
            <label className="block text-gray-300 mb-1">Password</label>
            <input 
              type="password" 
              className="w-full bg-gray-700 text-white border border-gray-600 rounded py-2 px-3 focus:outline-none focus:border-indigo-500"
            />
          </div>
          
          <button 
            className="w-full bg-indigo-600 hover:bg-indigo-500 text-white font-bold py-2 px-4 rounded"
            onClick={onLogin}
          >
            Login
          </button>
          
          <div className="text-center mt-4">
            <a href="#" className="text-indigo-400 hover:text-indigo-300 text-sm">
              Create Account
            </a>
          </div>
        </div>
      </div>
    </div>
  );
};

/**
 * Main game component
 */
const Game = () => {
  // Apollo client
  const apolloClient = useApollo({});
  
  // Auth state
  const [auth, setAuth] = useState<Auth>({
    isAuthenticated: false,
    userId: null,
  });
  
  // Cat selection state
  const [selectedCatId, setSelectedCatId] = useState<string | null>(null);
  
  // Handle login
  const handleLogin = () => {
    // In a real app, you'd verify credentials
    setAuth({
      isAuthenticated: true,
      userId: 'user123',
    });
  };
  
  // Handle cat selection
  const handleCatSelect = (catId: string) => {
    setSelectedCatId(catId);
  };
  
  // For development, auto-login
  useEffect(() => {
    if (process.env.NODE_ENV === 'development') {
      handleLogin();
      setSelectedCatId('1');
    }
  }, []);
  
  // Not authenticated - Show login
  if (!auth.isAuthenticated) {
    return <Login onLogin={handleLogin} />;
  }
  
  // Authenticated but no cat selected - Show character selection
  if (!selectedCatId) {
    return <CharacterSelection onSelect={handleCatSelect} />;
  }
  
  // Authenticated and cat selected - Show game
  return (
    <ApolloProvider client={apolloClient}>
      <GameProvider catId={selectedCatId}>
        <div className="game-container">
          <GameScene />
          <GameInterface />
        </div>
      </GameProvider>
    </ApolloProvider>
  );
};

export default Game;
