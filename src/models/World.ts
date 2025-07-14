import mongoose, { Document, Schema, Model } from 'mongoose';

/**
 * Interface for Zone types
 */
export enum ZoneType {
  NORMAL = 'normal',
  PVP = 'pvp',
  SAFE = 'safe',
  BANK = 'bank',
  SHOP = 'shop',
}

/**
 * Interface for World Zone document
 */
export interface IZone {
  name: string;
  type: ZoneType;
  bounds: {
    minX: number;
    maxX: number;
    minZ: number;
    maxZ: number;
  };
  isPvpAtNight: boolean;
}

/**
 * Interface for World document
 */
export interface IWorld extends Document {
  name: string;
  description: string;
  terrain: {
    heightmap: string; // Path to heightmap image
    size: number;
    maxHeight: number;
  };
  zones: IZone[];
  dayCycleMinutes: number;
  nightCycleMinutes: number;
  currentTime: number; // 0-1 value representing current time (0 = midnight)
  isNight: boolean;
  createdAt: Date;
  updatedAt: Date;
}

/**
 * Define world schema
 */
const WorldSchema = new Schema<IWorld>(
  {
    name: {
      type: String,
      required: true,
      unique: true,
    },
    description: {
      type: String,
      required: true,
    },
    terrain: {
      heightmap: {
        type: String,
        required: true,
      },
      size: {
        type: Number,
        default: 2000,
      },
      maxHeight: {
        type: Number,
        default: 100,
      },
    },
    zones: [
      {
        name: {
          type: String,
          required: true,
        },
        type: {
          type: String,
          enum: Object.values(ZoneType),
          default: ZoneType.NORMAL,
        },
        bounds: {
          minX: {
            type: Number,
            required: true,
          },
          maxX: {
            type: Number,
            required: true,
          },
          minZ: {
            type: Number,
            required: true,
          },
          maxZ: {
            type: Number,
            required: true,
          },
        },
        isPvpAtNight: {
          type: Boolean,
          default: true,
        },
      },
    ],
    dayCycleMinutes: {
      type: Number,
      default: 120,
    },
    nightCycleMinutes: {
      type: Number,
      default: 40,
    },
    currentTime: {
      type: Number,
      default: 0.5, // Start at noon
      min: 0,
      max: 1,
    },
    isNight: {
      type: Boolean,
      default: false,
    },
  },
  {
    timestamps: true,
  }
);

// Create and export the model
const World: Model<IWorld> = mongoose.models.World || mongoose.model<IWorld>('World', WorldSchema);

export default World; 