import { useRef, useEffect } from 'react';
import { useThree } from '@react-three/fiber';
import * as THREE from 'three';
import { useGameStore } from '@/lib/store/gameStore';

/**
 * ThreeJS Editor Integration
 * This component allows exporting the current scene to be edited in the official Three.js editor
 */
const ThreeJSEditorExporter: React.FC = () => {
  const { scene } = useThree();
  
  // Function to export the scene for the Three.js editor
  const exportScene = () => {
    try {
      // Convert scene to JSON
      const sceneJSON = scene.toJSON();
      
      // Convert to a string blob
      const jsonString = JSON.stringify(sceneJSON, null, 2);
      
      // Create a blob from the JSON string
      const blob = new Blob([jsonString], { type: 'application/json' });
      
      // Create a download link
      const url = URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = url;
      link.download = 'cat-annihilation-scene.json';
      
      // Trigger download
      document.body.appendChild(link);
      link.click();
      
      // Clean up
      document.body.removeChild(link);
      URL.revokeObjectURL(url);
      
      console.log('Scene exported successfully! Import it at https://threejs.org/editor/');
    } catch (error) {
      console.error('Error exporting scene:', error);
    }
  };

  // Make the export function available globally for debugging
  useEffect(() => {
    // @ts-ignore
    window.exportScene = exportScene;
    
    return () => {
      // @ts-ignore
      delete window.exportScene;
    };
  }, [scene]);

  return null;
};

/**
 * Simple UI for the Three.js editor export functionality
 */
export const EditorUI: React.FC = () => {
  const editorMode = useGameStore((state) => state.editorMode);
  
  if (!editorMode.isActive) return null;
  
  const handleExport = () => {
    // @ts-ignore
    if (window.exportScene) {
      // @ts-ignore
      window.exportScene();
    } else {
      console.error('Export function not available');
    }
  };
  
  const openThreeJSEditor = () => {
    window.open('https://threejs.org/editor/', '_blank');
  };
  
  return (
    <div className="absolute top-4 left-4 p-4 bg-black bg-opacity-70 rounded-lg text-white pointer-events-auto">
      <h2 className="text-xl font-bold mb-4">Three.js Editor Integration</h2>
      <div className="space-y-4">
        <div>
          <p className="mb-2">Export your scene to edit in the official Three.js editor:</p>
          <button 
            onClick={handleExport}
            className="bg-blue-600 hover:bg-blue-500 px-4 py-2 rounded font-bold"
          >
            Export Scene
          </button>
        </div>
        
        <div>
          <p className="mb-2">After exporting, open the Three.js editor:</p>
          <button 
            onClick={openThreeJSEditor}
            className="bg-green-600 hover:bg-green-500 px-4 py-2 rounded font-bold"
          >
            Open Three.js Editor
          </button>
        </div>
        
        <div className="mt-4 text-sm">
          <h3 className="text-lg font-bold mb-1">Instructions:</h3>
          <ol className="list-decimal pl-5 space-y-1">
            <li>Click "Export Scene" to download your scene as JSON</li>
            <li>Click "Open Three.js Editor" (or go to <a href="https://threejs.org/editor/" target="_blank" rel="noopener noreferrer" className="text-blue-400 hover:underline">threejs.org/editor</a>)</li>
            <li>In the Three.js editor, click File &gt; Import</li>
            <li>Select your downloaded JSON file</li>
            <li>Edit your scene using the Three.js editor's powerful tools</li>
            <li>When finished, in the editor click File &gt; Export Scene</li>
            <li>Import the exported file back into your game</li>
          </ol>
        </div>
      </div>
    </div>
  );
};

/**
 * Main EditorTools component
 */
const EditorTools: React.FC = () => {
  const editorMode = useGameStore((state) => state.editorMode);
  
  if (!editorMode.isActive) return null;
  
  return <ThreeJSEditorExporter />;
};

export default EditorTools; 