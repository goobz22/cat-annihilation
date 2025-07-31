import React, { useRef, useEffect, useState, memo } from 'react';
import { useFrame } from '@react-three/fiber';
import * as THREE from 'three';

interface CatMeshProps {
  isMoving: boolean;
  isRunning: boolean;
  isJumping: boolean;
  isAttacking: boolean;
  isDefending: boolean;
}

/**
 * Cat character 3D mesh with animations
 */
const CatMesh = memo(({ 
  isMoving, 
  isRunning,
  isJumping,
  isAttacking,
  isDefending 
}: CatMeshProps) => {
  const group = useRef<THREE.Group>(null);
  const bodyGroup = useRef<THREE.Group>(null);
  const catGroup = useRef<THREE.Group>(null);
  
  // Animation state
  const [animState, setAnimState] = useState({
    walkTime: 0,
    attackTime: 0,
    defendTime: 0,
    headRotation: 0,
    tailRotation: 0,
  });
  
  // Fur rendering setup
  useEffect(() => {
    if (!catGroup.current || !bodyGroup.current) return;
    
    const furLayers = 20;
    const furLength = 0.05;
    
    const baseGeometry = new THREE.CapsuleGeometry(0.35, 1.0, 8, 16);
    const furMaterial = new THREE.ShaderMaterial({
      uniforms: {
        layer: { value: 0 },
        furLength: { value: furLength },
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
        float noise(vec2 p) {
          return (sin(p.y * 20.0) * 0.5 + 0.5) * (fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453) * 0.2 + 0.8);
        }
        void main() {
          float density = noise(vUv + vec2(0.0, layer * 0.05));
          float threshold = layer / float(${furLayers});
          if (density < threshold * 1.2) discard;
          float alpha = (1.0 - threshold) * (0.5 + density * 0.5);
          vec3 color = mix(vec3(0.588, 0.294, 0.0), vec3(0.4, 0.2, 0.0), threshold);
          gl_FragColor = vec4(color, alpha);
        }
      `,
      transparent: true,
      side: THREE.DoubleSide,
      depthWrite: false,
    });

    // Body fur
    for (let i = 1; i <= furLayers; i++) {
      const layerMesh = new THREE.Mesh(baseGeometry, furMaterial.clone());
      layerMesh.material.uniforms.layer.value = i;
      bodyGroup.current.add(layerMesh);
    }

    // Head fur
    const headGeometry = new THREE.SphereGeometry(0.3, 16, 16);
    for (let i = 1; i <= furLayers; i++) {
      const layerMesh = new THREE.Mesh(headGeometry, furMaterial.clone());
      layerMesh.material.uniforms.layer.value = i;
      layerMesh.position.set(0, 0.5, 0.6);
      catGroup.current.add(layerMesh);
    }

    // Tail fur
    const tailGeometry = new THREE.CylinderGeometry(0.075, 0.05, 0.6, 8);
    for (let i = 1; i <= furLayers; i++) {
      const layerMesh = new THREE.Mesh(tailGeometry, furMaterial.clone());
      layerMesh.material.uniforms.layer.value = i;
      layerMesh.position.set(0, 0.2, -0.5);
      layerMesh.rotation.set(0.1, 0, 0);
      catGroup.current.add(layerMesh);
    }
  }, []);
  
  // Animation updates
  useFrame((_, delta) => {
    if (!group.current || !catGroup.current || !bodyGroup.current) return;
    
    const newAnimState = { ...animState };
    let stateChanged = false;
    
    // Get references to mesh parts more safely
    const bodyMesh = bodyGroup.current;
    const headMesh = catGroup.current.children.find(child => 
      child.type === 'Mesh' && child.position.y > 0.4 && child.position.z > 0.5
    ) as THREE.Mesh;
    const tailMesh = catGroup.current.children.find(child => 
      child.type === 'Mesh' && child.position.z < -0.6
    ) as THREE.Mesh;
    
    if (isMoving && bodyMesh && headMesh && tailMesh) {
      const animSpeed = isRunning ? 10 : 5;
      const oldWalkTime = newAnimState.walkTime;
      newAnimState.walkTime += delta * animSpeed;
      if (newAnimState.walkTime !== oldWalkTime) stateChanged = true;
      
      const walkCycle = Math.sin(newAnimState.walkTime);
      
      // Body animation - animate the main body mesh in bodyGroup
      const actualBody = group.current;
      if (actualBody) {
        const heightAdjustment = isRunning ? 0.15 : 0.1;
        // Keep body at fixed height
        actualBody.position.y = 0.2;
      }
      
      // Head bobbing
      if (headMesh && headMesh.rotation) {
        const headRotation = isRunning ? 0.15 : 0.1;
        headMesh.rotation.x = walkCycle * headRotation;
      }
      
      // Tail wagging
      if (tailMesh && tailMesh.rotation) {
        const tailWagSpeed = isRunning ? 3 : 2;
        const tailWagAmount = isRunning ? 0.3 : 0.2;
        const oldTailRotation = newAnimState.tailRotation;
        newAnimState.tailRotation = Math.sin(newAnimState.walkTime * tailWagSpeed) * tailWagAmount;
        if (newAnimState.tailRotation !== oldTailRotation) stateChanged = true;
        tailMesh.rotation.z = newAnimState.tailRotation;
      }
    }
    
    if (isAttacking) {
      const oldAttackTime = newAnimState.attackTime;
      newAnimState.attackTime += delta * 10;
      if (newAnimState.attackTime !== oldAttackTime) stateChanged = true;
      
      const attackPhase = Math.min(1, (newAnimState.attackTime % 1) * 2);
      const attackOffset = attackPhase < 0.5 
        ? attackPhase * 2 
        : 1 - ((attackPhase - 0.5) * 2);
      
      catGroup.current.position.z = attackOffset * 0.3;
      
      if (newAnimState.walkTime !== 0) {
        newAnimState.walkTime = 0;
        stateChanged = true;
      }
    } else if (isDefending && bodyMesh && headMesh && tailMesh) {
      const oldDefendTime = newAnimState.defendTime;
      newAnimState.defendTime += delta * 5;
      if (newAnimState.defendTime !== oldDefendTime) stateChanged = true;
      
      const actualBody = group.current;
      if (actualBody) {
        actualBody.position.y = 0.15;
      }
      
      if (headMesh && headMesh.rotation) {
        headMesh.rotation.x = 0.2;
      }
      
      if (tailMesh && tailMesh.rotation) {
        tailMesh.rotation.x = 0.5;
      }
      
      const defendPhase = Math.sin(newAnimState.defendTime * 3);
      catGroup.current.rotation.y = defendPhase * 0.1;
    } else {
      if (newAnimState.attackTime !== 0) {
        newAnimState.attackTime = 0;
        stateChanged = true;
      }
      if (newAnimState.defendTime !== 0) {
        newAnimState.defendTime = 0;
        stateChanged = true;
      }
      catGroup.current.position.z = 0;
      catGroup.current.rotation.y = 0;
    }
    
    // Idle animation
    if (!isMoving && !isJumping && !isAttacking && !isDefending && bodyMesh && tailMesh) {
      const oldWalkTime = newAnimState.walkTime;
      newAnimState.walkTime += delta;
      if (newAnimState.walkTime !== oldWalkTime) stateChanged = true;
      
      const breathCycle = Math.sin(newAnimState.walkTime);
      
      const actualBody = group.current;
      if (actualBody) {
        // Keep body at fixed height during idle
        actualBody.position.y = 0.2;
      }
      
      if (tailMesh && tailMesh.rotation) {
        const oldTailRotation = newAnimState.tailRotation;
        newAnimState.tailRotation = Math.sin(newAnimState.walkTime * 0.5) * 0.1;
        if (newAnimState.tailRotation !== oldTailRotation) stateChanged = true;
        tailMesh.rotation.z = newAnimState.tailRotation;
      }
    }
    
    // Update animation state if significant changes occurred
    if (stateChanged && (
      Math.abs(newAnimState.walkTime - animState.walkTime) > 0.01 ||
      Math.abs(newAnimState.tailRotation - animState.tailRotation) > 0.001 ||
      Math.abs(newAnimState.attackTime - animState.attackTime) > 0.01 ||
      Math.abs(newAnimState.defendTime - animState.defendTime) > 0.01
    )) {
      setAnimState(newAnimState);
    }
  });

  return (
    <group ref={catGroup}>
      <group ref={bodyGroup}>
        <mesh ref={group} castShadow position={[0, 0.2, 0]}>
          <capsuleGeometry args={[0.35, 1.0, 8, 16]} />
          <meshStandardMaterial color="#964B00" />
        </mesh>
      </group>
      
      {/* Head */}
      <mesh castShadow position={[0, 0.5, 0.6]}>
        <sphereGeometry args={[0.3, 16, 16]} />
        <meshStandardMaterial color="#964B00" />
      </mesh>
      
      {/* Eyes */}
      <group position={[0, 0.5, 0.7]}>
        <mesh position={[0.15, 0, 0.15]} castShadow>
          <sphereGeometry args={[0.08, 8, 8]} />
          <meshStandardMaterial color="#000000" />
        </mesh>
        <mesh position={[-0.15, 0, 0.15]} castShadow>
          <sphereGeometry args={[0.08, 8, 8]} />
          <meshStandardMaterial color="#000000" />
        </mesh>
      </group>
      
      {/* Tail */}
      <mesh castShadow position={[0, 0.3, -0.7]}>
        <cylinderGeometry args={[0.075, 0.05, 0.6, 8]} />
        <meshStandardMaterial color="#964B00" />
      </mesh>
      
      {/* Ears */}
      <group position={[0, 0.6, 0.5]}>
        <mesh position={[0.25, 0.15, 0]} castShadow>
          <coneGeometry args={[0.1, 0.2, 4]} />
          <meshStandardMaterial color="#964B00" />
        </mesh>
        <mesh position={[-0.25, 0.15, 0]} castShadow>
          <coneGeometry args={[0.1, 0.2, 4]} />
          <meshStandardMaterial color="#964B00" />
        </mesh>
      </group>
      
      {/* Legs */}
      <group position={[0, -0.1, 0]}>
        <mesh position={[0.25, -0.2, 0.5]} castShadow>
          <cylinderGeometry args={[0.06, 0.06, 0.4, 8]} />
          <meshStandardMaterial color="#7a3a00" />
        </mesh>
        <mesh position={[-0.25, -0.2, 0.5]} castShadow>
          <cylinderGeometry args={[0.06, 0.06, 0.4, 8]} />
          <meshStandardMaterial color="#7a3a00" />
        </mesh>
        <mesh position={[0.25, -0.2, -0.3]} castShadow>
          <cylinderGeometry args={[0.06, 0.06, 0.4, 8]} />
          <meshStandardMaterial color="#7a3a00" />
        </mesh>
        <mesh position={[-0.25, -0.2, -0.3]} castShadow>
          <cylinderGeometry args={[0.06, 0.06, 0.4, 8]} />
          <meshStandardMaterial color="#7a3a00" />
        </mesh>
      </group>

      {/* Facial features */}
      <mesh position={[0, 0.4, 0.9]} castShadow>
        <sphereGeometry args={[0.05, 8, 8]} />
        <meshStandardMaterial color="#000000" />
      </mesh>
      
      <mesh position={[0, 0.3, 0.85]}>
        <boxGeometry args={[0.15, 0.05, 0.01]} />
        <meshStandardMaterial color="#000000" emissive="#000000" emissiveIntensity={1} />
      </mesh>
      
      {/* Whiskers */}
      <group position={[0, 0.4, 0.8]}>
        <mesh position={[-0.2, 0, 0]} rotation={[0, 0, Math.PI / 2]}>
          <cylinderGeometry args={[0.01, 0.01, 0.3, 4]} />
          <meshStandardMaterial color="#FFFFFF" />
        </mesh>
        <mesh position={[-0.2, 0.05, 0]} rotation={[0, 0, Math.PI / 2]}>
          <cylinderGeometry args={[0.01, 0.01, 0.3, 4]} />
          <meshStandardMaterial color="#FFFFFF" />
        </mesh>
        <mesh position={[0.2, 0, 0]} rotation={[0, 0, Math.PI / 2]}>
          <cylinderGeometry args={[0.01, 0.01, 0.3, 4]} />
          <meshStandardMaterial color="#FFFFFF" />
        </mesh>
        <mesh position={[0.2, 0.05, 0]} rotation={[0, 0, Math.PI / 2]}>
          <cylinderGeometry args={[0.01, 0.01, 0.3, 4]} />
          <meshStandardMaterial color="#FFFFFF" />
        </mesh>
      </group>
    </group>
  );
});

CatMesh.displayName = 'CatMesh';

export default CatMesh;