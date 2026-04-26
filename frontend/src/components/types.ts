export interface Dimensions {
  llarg: number; // Eix X
  ample: number; // Eix Z (profunditat)
  alt: number;   // Eix Y (altura)
}

export interface WarehouseObject {
  id: string;
  tipus: 'estanteria' | 'obstacle';
  x: number;
  z: number;
  w: number; // Ample de l'objecte
  d: number; // Profunditat de l'objecte
  h: number; // Altura de l'objecte
  color: string;
}

export interface WarehouseData {
  magatzem: {
    dimensions: Dimensions;
  };
  objectes: WarehouseObject[];
}