import React, { useRef, useEffect, memo } from 'react';
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

  // Animation timers live in refs. Storing them in React state forced a re-render
  // every frame (the gated `setAnimState` at the end of useFrame still fired the
  // common case), and React state has no business in a useFrame loop per ARCHITECTURE.md.
  const walkTimeRef = useRef(0);
  const attackTimeRef = useRef(0);
  const defendTimeRef = useRef(0);

  // Fur rendering setup.
  //
  // The previous version created 20 fur-layer ShaderMaterials and 20 fur Meshes once
  // and attached them to the bodyGroup / catGroup, but never disposed any of them on
  // unmount — every CatCharacter unmount (e.g. error-boundary recovery) leaked ~60
  // GPU objects (20 body + 20 head + 20 tail meshes, each with a cloned ShaderMaterial).
  // We now track everything we add and dispose it on cleanup.
  useEffect(() => {
    const bodyGroupNode = bodyGroup.current;
    const catGroupNode = catGroup.current;
    if (!catGroupNode || !bodyGroupNode) return;

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

    const createdMaterials: THREE.ShaderMaterial[] = [];
    const bodyMeshes: THREE.Mesh[] = [];
    const headMeshes: THREE.Mesh[] = [];
    const tailMeshes: THREE.Mesh[] = [];

    // Body fur
    for (let i = 1; i <= furLayers; i++) {
      const layerMaterial = furMaterial.clone();
      layerMaterial.uniforms.layer.value = i;
      const layerMesh = new THREE.Mesh(baseGeometry, layerMaterial);
      bodyGroupNode.add(layerMesh);
      bodyMeshes.push(layerMesh);
      createdMaterials.push(layerMaterial);
    }

    // Head fur
    const headGeometry = new THREE.SphereGeometry(0.3, 16, 16);
    for (let i = 1; i <= furLayers; i++) {
      const layerMaterial = furMaterial.clone();
      layerMaterial.uniforms.layer.value = i;
      const layerMesh = new THREE.Mesh(headGeometry, layerMaterial);
      layerMesh.position.set(0, 0.5, 0.6);
      catGroupNode.add(layerMesh);
      headMeshes.push(layerMesh);
      createdMaterials.push(layerMaterial);
    }

    // Tail fur
    const tailGeometry = new THREE.CylinderGeometry(0.075, 0.05, 0.6, 8);
    for (let i = 1; i <= furLayers; i++) {
      const layerMaterial = furMaterial.clone();
      layerMaterial.uniforms.layer.value = i;
      const layerMesh = new THREE.Mesh(tailGeometry, layerMaterial);
      layerMesh.position.set(0, 0.2, -0.5);
      layerMesh.rotation.set(0.1, 0, 0);
      catGroupNode.add(layerMesh);
      tailMeshes.push(layerMesh);
      createdMaterials.push(layerMaterial);
    }

    // The template material itself never gets attached; dispose it so we don't keep a
    // phantom GPU shader program around for the component's lifetime.
    furMaterial.dispose();

    return () => {
      for (const mesh of bodyMeshes) bodyGroupNode.remove(mesh);
      for (const mesh of headMeshes) catGroupNode.remove(mesh);
      for (const mesh of tailMeshes) catGroupNode.remove(mesh);
      for (const material of createdMaterials) material.dispose();
      baseGeometry.dispose();
      headGeometry.dispose();
      tailGeometry.dispose();
    };
  }, []);
  
  // Animation updates.
  //
  // Drives three.js node rotations / positions directly from per-frame timer refs. The
  // previous implementation maintained a parallel `animState` React state object and
  // called setAnimState() inside useFrame whenever any timer moved by > 0.01s — which
  // is every frame in practice. That forced a React re-render every frame for a
  // component that has no render-time-dependent JSX (the mesh tree is static), wasting
  // reconciler work and violating the ARCHITECTURE.md rule against React state in
  // useFrame hot paths.
  useFrame((_, delta) => {
    if (!group.current || !catGroup.current || !bodyGroup.current) return;

    // Find mesh handles by their geometric role. These positional checks are inherited
    // from the previous implementation — the JSX places head at (0, 0.5, 0.6) and tail
    // at (0, 0.3, -0.7), so the y/z filters below uniquely identify each.
    const headMesh = catGroup.current.children.find(child =>
      child.type === 'Mesh' && child.position.y > 0.4 && child.position.z > 0.5
    ) as THREE.Mesh | undefined;
    const tailMesh = catGroup.current.children.find(child =>
      child.type === 'Mesh' && child.position.z < -0.6
    ) as THREE.Mesh | undefined;

    if (isMoving && headMesh && tailMesh) {
      const animSpeed = isRunning ? 10 : 5;
      walkTimeRef.current += delta * animSpeed;

      const walkCycle = Math.sin(walkTimeRef.current);

      // Keep body at fixed height during the walk cycle.
      group.current.position.y = 0.2;

      // Head bobbing
      const headRotationAmount = isRunning ? 0.15 : 0.1;
      headMesh.rotation.x = walkCycle * headRotationAmount;

      // Tail wagging
      const tailWagSpeed = isRunning ? 3 : 2;
      const tailWagAmount = isRunning ? 0.3 : 0.2;
      tailMesh.rotation.z = Math.sin(walkTimeRef.current * tailWagSpeed) * tailWagAmount;
    }

    if (isAttacking) {
      attackTimeRef.current += delta * 10;
      const attackPhase = Math.min(1, (attackTimeRef.current % 1) * 2);
      const attackOffset = attackPhase < 0.5
        ? attackPhase * 2
        : 1 - ((attackPhase - 0.5) * 2);

      catGroup.current.position.z = attackOffset * 0.3;
      walkTimeRef.current = 0;
    } else if (isDefending && headMesh && tailMesh) {
      defendTimeRef.current += delta * 5;
      group.current.position.y = 0.15;
      headMesh.rotation.x = 0.2;
      tailMesh.rotation.x = 0.5;
      const defendPhase = Math.sin(defendTimeRef.current * 3);
      catGroup.current.rotation.y = defendPhase * 0.1;
    } else {
      attackTimeRef.current = 0;
      defendTimeRef.current = 0;
      catGroup.current.position.z = 0;
      catGroup.current.rotation.y = 0;
    }

    // Idle animation — slow tail swish when nothing else is happening.
    if (!isMoving && !isJumping && !isAttacking && !isDefending && tailMesh) {
      walkTimeRef.current += delta;
      group.current.position.y = 0.2;
      tailMesh.rotation.z = Math.sin(walkTimeRef.current * 0.5) * 0.1;
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