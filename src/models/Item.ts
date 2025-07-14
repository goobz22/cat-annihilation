import mongoose, { Document, Schema, Model } from 'mongoose';

/**
 * Interface for item types
 */
export enum ItemType {
  ARMOR = 'armor',
  WEAPON = 'weapon',
  ACCESSORY = 'accessory',
  CONSUMABLE = 'consumable',
  COLLECTIBLE = 'collectible',
  CURRENCY = 'currency',
}

/**
 * Interface for Item document
 */
export interface IItem extends Document {
  name: string;
  description: string;
  type: ItemType;
  rarity: 'common' | 'uncommon' | 'rare' | 'epic' | 'legendary';
  value: number;
  isStackable: boolean;
  maxStackSize: number;
  stats: {
    attack?: number;
    defense?: number;
    health?: number;
    speed?: number;
  };
  modelPath?: string;
  texturePath?: string;
  createdAt: Date;
  updatedAt: Date;
}

/**
 * Define item schema
 */
const ItemSchema = new Schema<IItem>(
  {
    name: {
      type: String,
      required: true,
      trim: true,
      unique: true,
    },
    description: {
      type: String,
      required: true,
    },
    type: {
      type: String,
      enum: Object.values(ItemType),
      required: true,
    },
    rarity: {
      type: String,
      enum: ['common', 'uncommon', 'rare', 'epic', 'legendary'],
      default: 'common',
    },
    value: {
      type: Number,
      required: true,
      min: 0,
    },
    isStackable: {
      type: Boolean,
      default: false,
    },
    maxStackSize: {
      type: Number,
      default: 1,
    },
    stats: {
      attack: {
        type: Number,
        default: 0,
      },
      defense: {
        type: Number,
        default: 0,
      },
      health: {
        type: Number,
        default: 0,
      },
      speed: {
        type: Number,
        default: 0,
      },
    },
    modelPath: {
      type: String,
    },
    texturePath: {
      type: String,
    },
  },
  {
    timestamps: true,
  }
);

// Create and export the model
const Item: Model<IItem> = mongoose.models.Item || mongoose.model<IItem>('Item', ItemSchema);

export default Item; 