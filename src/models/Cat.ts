import mongoose, { Document, Schema, Model } from 'mongoose';

/**
 * Interface for Cat document
 */
export interface ICat extends Document {
  name: string;
  owner: string;
  level: number;
  experience: number;
  health: number;
  maxHealth: number;
  attack: number;
  defense: number;
  speed: number;
  currency: number;
  inventory: {
    items: {
      itemId: string;
      quantity: number;
    }[];
    maxSize: number;
  };
  equipment: {
    armor: string | null;
    claws: string | null;
    accessory: string | null;
  };
  position: {
    x: number;
    y: number;
    z: number;
  };
  rotation: number;
  createdAt: Date;
  updatedAt: Date;
}

/**
 * Define cat schema
 */
const CatSchema = new Schema<ICat>(
  {
    name: {
      type: String,
      required: true,
      trim: true,
      minlength: 3,
      maxlength: 20,
    },
    owner: {
      type: String,
      required: true,
      index: true,
    },
    level: {
      type: Number,
      default: 1,
      min: 1,
    },
    experience: {
      type: Number,
      default: 0,
      min: 0,
    },
    health: {
      type: Number,
      default: 100,
    },
    maxHealth: {
      type: Number,
      default: 100,
    },
    attack: {
      type: Number,
      default: 10,
    },
    defense: {
      type: Number,
      default: 5,
    },
    speed: {
      type: Number,
      default: 5,
    },
    currency: {
      type: Number,
      default: 0,
      min: 0,
    },
    inventory: {
      items: [{
        itemId: {
          type: String,
          required: true,
        },
        quantity: {
          type: Number,
          required: true,
          min: 1,
        },
      }],
      maxSize: {
        type: Number,
        default: 20,
      },
    },
    equipment: {
      armor: {
        type: String,
        default: null,
      },
      claws: {
        type: String,
        default: null,
      },
      accessory: {
        type: String,
        default: null,
      },
    },
    position: {
      x: {
        type: Number,
        default: 0,
      },
      y: {
        type: Number,
        default: 0,
      },
      z: {
        type: Number,
        default: 0,
      },
    },
    rotation: {
      type: Number,
      default: 0,
    },
  },
  {
    timestamps: true,
  }
);

// Create and export the model
const Cat: Model<ICat> = mongoose.models.Cat || mongoose.model<ICat>('Cat', CatSchema);

export default Cat; 