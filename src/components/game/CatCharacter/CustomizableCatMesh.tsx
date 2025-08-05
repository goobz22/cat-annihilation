import React, { useRef, useEffect, useState, memo } from 'react';
import { useFrame } from '@react-three/fiber';
import * as THREE from 'three';

export interface CatCustomization {
  // Base colors
  primaryColor: string;
  secondaryColor?: string;
  eyeColor: string;
  noseColor: string;
  pawColor?: string;
  
  // Patterns
  pattern?: 'solid' | 'tabby' | 'calico' | 'tuxedo' | 'siamese' | 'spots';
  patternColor?: string;
  
  // Physical traits
  earSize?: 'small' | 'normal' | 'large';
  tailLength?: 'short' | 'normal' | 'long';
  furLength?: 'short' | 'medium' | 'long';
  bodyType?: 'slim' | 'normal' | 'chubby';
  
  // Accessories
  collar?: {
    color: string;
    hasTag?: boolean;
  };
  scars?: Array<{
    position: 'eye' | 'ear' | 'body';
    side?: 'left' | 'right';
  }>;
}

interface CustomizableCatMeshProps {
  customization: CatCustomization;
  isMoving?: boolean;
  isRunning?: boolean;
  isJumping?: boolean;
  isAttacking?: boolean;
  isDefending?: boolean;
  scale?: number;
}

const defaultCustomization: CatCustomization = {
  primaryColor: '#964B00',
  eyeColor: '#4CAF50',
  noseColor: '#FF69B4',
  pattern: 'solid',
  earSize: 'normal',
  tailLength: 'normal',
  furLength: 'medium',
  bodyType: 'normal'
};

/**
 * Customizable cat character 3D mesh with various options
 */
const CustomizableCatMesh = memo(({ 
  customization = defaultCustomization,
  isMoving = false, 
  isRunning = false,
  isJumping = false,
  isAttacking = false,
  isDefending = false,
  scale = 1
}: CustomizableCatMeshProps) => {
  const group = useRef<THREE.Group>(null);
  const bodyGroup = useRef<THREE.Group>(null);
  const catGroup = useRef<THREE.Group>(null);
  
  // Animation state
  const [animState, setAnimState] = useState({
    walkTime: 0,
    attackTime: 0,
    defendTime: 0,
    tailRotation: 0,
  });
  
  // Merge with defaults
  const config = { ...defaultCustomization, ...customization };
  
  // Calculate body dimensions based on bodyType
  const bodyScale = config.bodyType === 'slim' ? 0.9 : config.bodyType === 'chubby' ? 1.15 : 1.0;
  const bodyRadius = 0.35 * bodyScale;
  const bodyHeight = 1.0;
  
  // Calculate tail length
  const tailLengthMultiplier = config.tailLength === 'short' ? 0.7 : config.tailLength === 'long' ? 1.3 : 1.0;
  const tailLength = 0.6 * tailLengthMultiplier;
  
  // Calculate ear size
  const earSizeMultiplier = config.earSize === 'small' ? 0.7 : config.earSize === 'large' ? 1.3 : 1.0;
  
  // Create pattern texture for use in materials
  const [patternTexture, setPatternTexture] = useState<THREE.Texture | null>(null);
  
  useEffect(() => {
    // Create pattern texture
    const createPatternTexture = () => {
      const canvas = document.createElement('canvas');
      canvas.width = 256;
      canvas.height = 256;
      const ctx = canvas.getContext('2d')!;
      
      // Base color
      ctx.fillStyle = config.primaryColor;
      ctx.fillRect(0, 0, 256, 256);
      
      if (config.pattern !== 'solid') {
        const patternColor = config.patternColor || config.secondaryColor || '#000000';
        ctx.fillStyle = patternColor;
        
        switch (config.pattern) {
          case 'tabby':
            // Striped pattern
            for (let i = 0; i < 256; i += 20) {
              ctx.beginPath();
              ctx.moveTo(i, 0);
              ctx.lineTo(i + 10, 256);
              ctx.lineTo(i + 15, 256);
              ctx.lineTo(i + 5, 0);
              ctx.fill();
            }
            break;
            
          case 'spots':
            // Spotted pattern
            for (let i = 0; i < 10; i++) {
              const x = Math.random() * 256;
              const y = Math.random() * 256;
              const radius = 10 + Math.random() * 20;
              ctx.beginPath();
              ctx.arc(x, y, radius, 0, Math.PI * 2);
              ctx.fill();
            }
            break;
            
          case 'tuxedo':
            // Tuxedo pattern (white chest/paws)
            ctx.fillStyle = '#FFFFFF';
            ctx.fillRect(80, 100, 96, 156); // Chest
            ctx.fillRect(0, 200, 256, 56); // Paws
            break;
            
          case 'calico':
            // Calico patches
            const colors = [config.patternColor, config.secondaryColor || '#FF6B35', '#000000'];
            for (let i = 0; i < 15; i++) {
              ctx.fillStyle = colors[i % colors.length];
              const x = Math.random() * 256;
              const y = Math.random() * 256;
              const w = 30 + Math.random() * 50;
              const h = 30 + Math.random() * 50;
              ctx.fillRect(x, y, w, h);
            }
            break;
            
          case 'siamese':
            // Siamese gradient (darker extremities)
            const gradient = ctx.createRadialGradient(128, 128, 50, 128, 128, 128);
            gradient.addColorStop(0, config.primaryColor);
            gradient.addColorStop(1, config.patternColor);
            ctx.fillStyle = gradient;
            ctx.fillRect(0, 0, 256, 256);
            break;
        }
      }
      
      return new THREE.CanvasTexture(canvas);
    };
    
    const texture = createPatternTexture();
    setPatternTexture(texture);
    return () => {
      texture.dispose();
    };
  }, [config.primaryColor, config.pattern, config.patternColor, config.secondaryColor]);
  
  // Fur rendering setup with customization
  useEffect(() => {
    if (!catGroup.current || !bodyGroup.current || !patternTexture) return;
    
    // Clear existing fur
    bodyGroup.current.clear();
    
    const furLayers = config.furLength === 'short' ? 10 : config.furLength === 'long' ? 30 : 20;
    const furLengthValue = config.furLength === 'short' ? 0.02 : config.furLength === 'long' ? 0.08 : 0.05;
    
    const baseGeometry = new THREE.CapsuleGeometry(bodyRadius, bodyHeight, 8, 16);
    const furMaterial = new THREE.ShaderMaterial({
      uniforms: {
        layer: { value: 0 },
        furLength: { value: furLengthValue },
        baseTexture: { value: patternTexture },
        primaryColor: { value: new THREE.Color(config.primaryColor) },
      },
      vertexShader: `
        varying vec2 vUv;
        varying vec3 vNormal;
        uniform float layer;
        uniform float furLength;
        void main() {
          vUv = uv;
          vNormal = normal;
          vec3 pos = position + normal * layer * (furLength / float(${furLayers}));
          pos.y -= layer * layer * 0.001;
          gl_Position = projectionMatrix * modelViewMatrix * vec4(pos, 1.0);
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        varying vec3 vNormal;
        uniform float layer;
        uniform sampler2D baseTexture;
        uniform vec3 primaryColor;
        float noise(vec2 p) {
          return (sin(p.y * 20.0) * 0.5 + 0.5) * (fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453) * 0.2 + 0.8);
        }
        void main() {
          float density = noise(vUv + vec2(0.0, layer * 0.05));
          float threshold = layer / float(${furLayers});
          if (density < threshold * 1.2) discard;
          float alpha = (1.0 - threshold) * (0.5 + density * 0.5);
          vec4 texColor = texture2D(baseTexture, vUv);
          vec3 color = mix(texColor.rgb, texColor.rgb * 0.7, threshold);
          gl_FragColor = vec4(color, alpha);
        }
      `,
      transparent: true,
      side: THREE.DoubleSide,
      depthWrite: false,
    });

    // Add fur layers
    for (let i = 1; i <= furLayers; i++) {
      const layerMesh = new THREE.Mesh(baseGeometry, furMaterial.clone());
      layerMesh.material.uniforms.layer.value = i;
      bodyGroup.current.add(layerMesh);
    }
  }, [config, patternTexture, bodyRadius, bodyHeight]);
  
  // Animation updates
  useFrame((_, delta) => {
    if (!group.current || !catGroup.current || !bodyGroup.current) return;
    
    const newAnimState = { ...animState };
    
    // Get references to mesh parts
    const tailMesh = catGroup.current.children.find(child => 
      child.userData.partType === 'tail'
    ) as THREE.Mesh;
    
    if (isMoving) {
      const animSpeed = isRunning ? 10 : 5;
      newAnimState.walkTime += delta * animSpeed;
      
      const walkCycle = Math.sin(newAnimState.walkTime);
      
      // Tail wagging
      if (tailMesh && tailMesh.rotation) {
        const tailWagSpeed = isRunning ? 3 : 2;
        const tailWagAmount = isRunning ? 0.3 : 0.2;
        newAnimState.tailRotation = Math.sin(newAnimState.walkTime * tailWagSpeed) * tailWagAmount;
        tailMesh.rotation.z = newAnimState.tailRotation;
      }
    }
    
    if (isAttacking) {
      newAnimState.attackTime += delta * 10;
      const attackPhase = Math.min(1, (newAnimState.attackTime % 1) * 2);
      const attackOffset = attackPhase < 0.5 
        ? attackPhase * 2 
        : 1 - ((attackPhase - 0.5) * 2);
      
      catGroup.current.position.z = attackOffset * 0.3;
    } else {
      catGroup.current.position.z = 0;
    }
    
    // Idle animation
    if (!isMoving && !isJumping && !isAttacking && !isDefending && tailMesh) {
      newAnimState.walkTime += delta;
      
      if (tailMesh && tailMesh.rotation) {
        newAnimState.tailRotation = Math.sin(newAnimState.walkTime * 0.5) * 0.1;
        tailMesh.rotation.z = newAnimState.tailRotation;
      }
    }
    
    setAnimState(newAnimState);
  });

  return (
    <group ref={catGroup} scale={scale}>
      <group ref={bodyGroup}>
        <mesh ref={group} castShadow position={[0, 0.2, 0]}>
          <capsuleGeometry args={[bodyRadius, bodyHeight, 8, 16]} />
          <meshStandardMaterial 
            map={patternTexture}
            color={config.pattern === 'solid' ? config.primaryColor : '#FFFFFF'}
          />
        </mesh>
      </group>
      
      {/* Head */}
      <mesh castShadow position={[0, 0.5, 0.6]}>
        <sphereGeometry args={[0.3, 16, 16]} />
        <meshStandardMaterial 
          map={patternTexture}
          color={config.pattern === 'solid' ? config.primaryColor : '#FFFFFF'}
        />
      </mesh>
      
      {/* Eyes */}
      <group position={[0, 0.5, 0.7]}>
        <mesh position={[0.15, 0, 0.15]} castShadow>
          <sphereGeometry args={[0.08, 8, 8]} />
          <meshStandardMaterial color={config.eyeColor} emissive={config.eyeColor} emissiveIntensity={0.3} />
        </mesh>
        <mesh position={[-0.15, 0, 0.15]} castShadow>
          <sphereGeometry args={[0.08, 8, 8]} />
          <meshStandardMaterial color={config.eyeColor} emissive={config.eyeColor} emissiveIntensity={0.3} />
        </mesh>
      </group>
      
      {/* Scars */}
      {config.scars?.map((scar, index) => {
        if (scar.position === 'eye') {
          const xPos = scar.side === 'left' ? -0.15 : 0.15;
          return (
            <mesh key={`scar-${index}`} position={[xPos, 0.55, 0.85]}>
              <boxGeometry args={[0.02, 0.15, 0.02]} />
              <meshStandardMaterial color="#8B4513" />
            </mesh>
          );
        }
        return null;
      })}
      
      {/* Tail */}
      <mesh 
        castShadow 
        position={[0, 0.3, -0.7]}
        userData={{ partType: 'tail' }}
      >
        <cylinderGeometry args={[0.075, 0.05, tailLength, 8]} />
        <meshStandardMaterial 
          map={patternTexture}
          color={config.pattern === 'solid' ? config.primaryColor : '#FFFFFF'}
        />
      </mesh>
      
      {/* Ears */}
      <group position={[0, 0.6, 0.5]}>
        <mesh position={[0.25, 0.15, 0]} castShadow>
          <coneGeometry args={[0.1 * earSizeMultiplier, 0.2 * earSizeMultiplier, 4]} />
          <meshStandardMaterial color={config.primaryColor} />
        </mesh>
        <mesh position={[-0.25, 0.15, 0]} castShadow>
          <coneGeometry args={[0.1 * earSizeMultiplier, 0.2 * earSizeMultiplier, 4]} />
          <meshStandardMaterial color={config.primaryColor} />
        </mesh>
      </group>
      
      {/* Legs */}
      <group position={[0, -0.1, 0]}>
        {[
          [0.25, -0.2, 0.5],
          [-0.25, -0.2, 0.5],
          [0.25, -0.2, -0.3],
          [-0.25, -0.2, -0.3]
        ].map((pos, i) => (
          <mesh key={`leg-${i}`} position={pos as [number, number, number]} castShadow>
            <cylinderGeometry args={[0.06, 0.06, 0.4, 8]} />
            <meshStandardMaterial color={config.pawColor || config.secondaryColor || config.primaryColor} />
          </mesh>
        ))}
      </group>

      {/* Nose */}
      <mesh position={[0, 0.4, 0.9]} castShadow>
        <sphereGeometry args={[0.05, 8, 8]} />
        <meshStandardMaterial color={config.noseColor} />
      </mesh>
      
      {/* Mouth */}
      <mesh position={[0, 0.3, 0.85]}>
        <boxGeometry args={[0.15, 0.05, 0.01]} />
        <meshStandardMaterial color="#000000" />
      </mesh>
      
      {/* Whiskers */}
      <group position={[0, 0.4, 0.8]}>
        {[
          [-0.2, 0, 0],
          [-0.2, 0.05, 0],
          [0.2, 0, 0],
          [0.2, 0.05, 0]
        ].map((pos, i) => (
          <mesh key={`whisker-${i}`} position={pos as [number, number, number]} rotation={[0, 0, Math.PI / 2]}>
            <cylinderGeometry args={[0.01, 0.01, 0.3, 4]} />
            <meshStandardMaterial color="#FFFFFF" />
          </mesh>
        ))}
      </group>
      
      {/* Collar */}
      {config.collar && (
        <group position={[0, 0.1, 0.4]}>
          <mesh>
            <torusGeometry args={[0.4, 0.03, 8, 32]} />
            <meshStandardMaterial color={config.collar.color} />
          </mesh>
          {config.collar.hasTag && (
            <mesh position={[0, -0.1, 0.4]}>
              <boxGeometry args={[0.1, 0.1, 0.02]} />
              <meshStandardMaterial color="#FFD700" metalness={0.8} roughness={0.2} />
            </mesh>
          )}
        </group>
      )}
    </group>
  );
});

CustomizableCatMesh.displayName = 'CustomizableCatMesh';

export default CustomizableCatMesh;