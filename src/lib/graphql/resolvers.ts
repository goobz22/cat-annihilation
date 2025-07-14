import { connectToDatabase } from '@/lib/db/connection';
import Cat from '@/models/Cat';
import Item, { ItemType } from '@/models/Item';
import World, { ZoneType } from '@/models/World';

/**
 * GraphQL resolvers
 */
export const resolvers = {
  Query: {
    /**
     * Get a cat by ID
     */
    getCat: async (_: any, { id }: { id: string }) => {
      await connectToDatabase();
      return await Cat.findById(id);
    },

    /**
     * Get a cat by owner ID
     */
    getCatByOwner: async (_: any, { owner }: { owner: string }) => {
      await connectToDatabase();
      return await Cat.findOne({ owner });
    },

    /**
     * Get all cats
     */
    getAllCats: async () => {
      await connectToDatabase();
      return await Cat.find({});
    },

    /**
     * Get an item by ID
     */
    getItem: async (_: any, { id }: { id: string }) => {
      await connectToDatabase();
      return await Item.findById(id);
    },

    /**
     * Get all items
     */
    getAllItems: async () => {
      await connectToDatabase();
      return await Item.find({});
    },

    /**
     * Get items by type
     */
    getItemsByType: async (_: any, { type }: { type: string }) => {
      await connectToDatabase();
      return await Item.find({ type });
    },

    /**
     * Get a world by ID
     */
    getWorld: async (_: any, { id }: { id: string }) => {
      await connectToDatabase();
      return await World.findById(id);
    },

    /**
     * Get the default world
     */
    getDefaultWorld: async () => {
      await connectToDatabase();
      return await World.findOne({});
    },
  },

  Mutation: {
    /**
     * Create a new cat
     */
    createCat: async (_: any, { input }: { input: any }) => {
      await connectToDatabase();
      return await Cat.create(input);
    },

    /**
     * Update a cat
     */
    updateCat: async (_: any, { id, input }: { id: string; input: any }) => {
      await connectToDatabase();
      return await Cat.findByIdAndUpdate(id, input, { new: true });
    },

    /**
     * Delete a cat
     */
    deleteCat: async (_: any, { id }: { id: string }) => {
      await connectToDatabase();
      const result = await Cat.findByIdAndDelete(id);
      return !!result;
    },

    /**
     * Equip an item on a cat
     */
    equipItem: async (_: any, { catId, itemId, slot }: { catId: string; itemId: string; slot: string }) => {
      await connectToDatabase();
      
      // Check if cat and item exist
      const cat = await Cat.findById(catId);
      const item = await Item.findById(itemId);
      
      if (!cat || !item) {
        throw new Error('Cat or item not found');
      }
      
      // Check if slot is valid
      if (!['armor', 'claws', 'accessory'].includes(slot)) {
        throw new Error('Invalid equipment slot');
      }
      
      // Check if item type matches slot
      const itemTypeMap: Record<string, ItemType> = {
        armor: ItemType.ARMOR,
        claws: ItemType.WEAPON,
        accessory: ItemType.ACCESSORY,
      };
      
      if (item.type !== itemTypeMap[slot]) {
        throw new Error(`Item type ${item.type} cannot be equipped in slot ${slot}`);
      }
      
      // Update cat equipment
      const updateData = {
        [`equipment.${slot}`]: itemId,
      };
      
      return await Cat.findByIdAndUpdate(catId, updateData, { new: true });
    },

    /**
     * Unequip an item from a cat
     */
    unequipItem: async (_: any, { catId, slot }: { catId: string; slot: string }) => {
      await connectToDatabase();
      
      // Check if cat exists
      const cat = await Cat.findById(catId);
      
      if (!cat) {
        throw new Error('Cat not found');
      }
      
      // Check if slot is valid
      if (!['armor', 'claws', 'accessory'].includes(slot)) {
        throw new Error('Invalid equipment slot');
      }
      
      // Update cat equipment
      const updateData = {
        [`equipment.${slot}`]: null,
      };
      
      return await Cat.findByIdAndUpdate(catId, updateData, { new: true });
    },

    /**
     * Add an item to a cat's inventory
     */
    addItemToCat: async (_: any, { catId, itemId, quantity }: { catId: string; itemId: string; quantity: number }) => {
      await connectToDatabase();
      
      // Check if cat and item exist
      const cat = await Cat.findById(catId);
      const item = await Item.findById(itemId);
      
      if (!cat || !item) {
        throw new Error('Cat or item not found');
      }
      
      // Check if quantity is valid
      if (quantity <= 0) {
        throw new Error('Quantity must be greater than 0');
      }
      
      // Check if inventory has space
      if (cat.inventory.items.length >= cat.inventory.maxSize && !cat.inventory.items.some(i => i.itemId === itemId)) {
        throw new Error('Inventory is full');
      }
      
      // Check if item is stackable
      const existingItemIndex = cat.inventory.items.findIndex(i => i.itemId === itemId);
      
      if (existingItemIndex !== -1) {
        // Update existing item quantity
        const newQuantity = cat.inventory.items[existingItemIndex].quantity + quantity;
        
        if (item.isStackable && newQuantity <= item.maxStackSize) {
          cat.inventory.items[existingItemIndex].quantity = newQuantity;
        } else if (item.isStackable) {
          throw new Error(`Cannot stack more than ${item.maxStackSize} of this item`);
        } else {
          throw new Error('This item cannot be stacked');
        }
      } else {
        // Add new item to inventory
        cat.inventory.items.push({
          itemId,
          quantity,
        });
      }
      
      return await cat.save();
    },

    /**
     * Remove an item from a cat's inventory
     */
    removeItemFromCat: async (_: any, { catId, itemId, quantity }: { catId: string; itemId: string; quantity: number }) => {
      await connectToDatabase();
      
      // Check if cat exists
      const cat = await Cat.findById(catId);
      
      if (!cat) {
        throw new Error('Cat not found');
      }
      
      // Check if quantity is valid
      if (quantity <= 0) {
        throw new Error('Quantity must be greater than 0');
      }
      
      // Find item in inventory
      const existingItemIndex = cat.inventory.items.findIndex(i => i.itemId === itemId);
      
      if (existingItemIndex === -1) {
        throw new Error('Item not found in inventory');
      }
      
      // Update or remove item
      const existingQuantity = cat.inventory.items[existingItemIndex].quantity;
      
      if (existingQuantity <= quantity) {
        // Remove item entirely
        cat.inventory.items.splice(existingItemIndex, 1);
      } else {
        // Reduce quantity
        cat.inventory.items[existingItemIndex].quantity = existingQuantity - quantity;
      }
      
      return await cat.save();
    },

    /**
     * Update a cat's position
     */
    updateCatPosition: async (_: any, { id, position }: { id: string; position: any }) => {
      await connectToDatabase();
      return await Cat.findByIdAndUpdate(id, { position }, { new: true });
    },

    /**
     * Create a new world
     */
    createWorld: async (_: any, { name, description }: { name: string; description: string }) => {
      await connectToDatabase();
      
      // Create default terrain and zones
      const worldData = {
        name,
        description,
        terrain: {
          heightmap: '/assets/terrain/default-heightmap.png',
          size: 2000,
          maxHeight: 100,
        },
        zones: [
          {
            name: 'Starting Area',
            type: ZoneType.SAFE,
            bounds: {
              minX: -100,
              maxX: 100,
              minZ: -100,
              maxZ: 100,
            },
            isPvpAtNight: false,
          },
        ],
        dayCycleMinutes: 120,
        nightCycleMinutes: 40,
        currentTime: 0.5,
        isNight: false,
      };
      
      return await World.create(worldData);
    },

    /**
     * Add a zone to a world
     */
    addZoneToWorld: async (_: any, { worldId, zone }: { worldId: string; zone: any }) => {
      await connectToDatabase();
      
      // Check if world exists
      const world = await World.findById(worldId);
      
      if (!world) {
        throw new Error('World not found');
      }
      
      // Add zone to world
      world.zones.push(zone);
      
      return await world.save();
    },

    /**
     * Update world time
     */
    updateWorldTime: async (_: any, { worldId, currentTime, isNight }: { worldId: string; currentTime: number; isNight: boolean }) => {
      await connectToDatabase();
      
      // Check if world exists
      const world = await World.findById(worldId);
      
      if (!world) {
        throw new Error('World not found');
      }
      
      // Update world time
      world.currentTime = currentTime;
      world.isNight = isNight;
      
      return await world.save();
    },
  },
};

export default resolvers; 