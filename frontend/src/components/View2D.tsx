import React from 'react';
import { Stage, Layer, Rect, Group, Text } from 'react-konva';
import type { WarehouseData } from './types';

interface Props {
  data: WarehouseData;
}

const View2D: React.FC<Props> = ({ data }) => {
  const escala = 30; // 1 metre = 30 píxels
  const offset = 50; // Marge superior/esquerre

  return (
    <Stage width={window.innerWidth} height={window.innerHeight}>
      <Layer>
        {/* Terra del magatzem */}
        <Rect
          x={offset}
          y={offset}
          width={data.magatzem.dimensions.llarg * escala}
          height={data.magatzem.dimensions.ample * escala}
          fill="#ffffff"
          stroke="#34495e"
          strokeWidth={2}
        />
        
        {data.objectes.map((obj) => (
          <Group key={obj.id}>
            <Rect
              x={offset + (obj.x * escala)}
              y={offset + (obj.z * escala)}
              width={obj.w * escala}
              height={obj.d * escala}
              fill={obj.color}
              shadowColor="black"
              shadowBlur={5}
              shadowOpacity={0.2}
            />
            <Text 
              text={obj.tipus}
              x={offset + (obj.x * escala)}
              y={offset + (obj.z * escala) - 15}
              fontSize={10}
            />
          </Group>
        ))}
      </Layer>
    </Stage>
  );
};

export default View2D;