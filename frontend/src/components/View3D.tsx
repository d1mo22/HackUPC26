import React from 'react';
import { Canvas } from '@react-three/fiber';
import { OrbitControls, Grid, PerspectiveCamera, ContactShadows } from '@react-three/drei';
import type { WarehouseData, WarehouseObject } from './types';

interface Props {
  data: WarehouseData;
}

const Box: React.FC<{ obj: WarehouseObject }> = ({ obj }) => {
  return (
    <mesh position={[obj.x + obj.w / 2, obj.h / 2, obj.z + obj.d / 2]}>
      <boxGeometry args={[obj.w, obj.h, obj.d]} />
      <meshStandardMaterial color={obj.color} />
    </mesh>
  );
};

const View3D: React.FC<Props> = ({ data }) => {
  const { llarg, ample } = data.magatzem.dimensions;

  return (
    <Canvas shadows>
      <PerspectiveCamera makeDefault position={[25, 20, 25]} />
      <OrbitControls makeDefault />
      
      {/* Il·luminació */}
      <ambientLight intensity={0.7} />
      <directionalLight position={[10, 20, 10]} castShadow intensity={1} />
      
      {/* Graella de referència i terra visual */}
      <Grid 
        infiniteGrid 
        sectionSize={5} 
        cellSize={1} 
        sectionColor="#3498db" 
        fadeDistance={50} 
      />
      
      <mesh rotation={[-Math.PI / 2, 0, 0]} position={[llarg / 2, -0.01, ample / 2]} receiveShadow>
        <planeGeometry args={[llarg, ample]} />
        <meshStandardMaterial color="#ecf0f1" />
      </mesh>

      {/* Renderitzat d'objectes */}
      {data.objectes.map((obj) => (
        <Box key={obj.id} obj={obj} />
      ))}

      <ContactShadows opacity={0.4} scale={30} blur={2.4} far={10} />
    </Canvas>
  );
};

export default View3D;