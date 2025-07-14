import { Server as SocketIOServer } from 'socket.io';
import { Server as HTTPServer } from 'http';
import { SocketEvents } from './client';
import { connectToDatabase } from '@/lib/db/connection';
import World, { IWorld } from '@/models/World';
import Cat, { ICat } from '@/models/Cat';

/**
 * Player data stored in memory
 */
interface IConnectedPlayer {
  socketId: string;
  cat: Partial<ICat> | null;
  position: {
    x: number;
    y: number;
    z: number;
    rotation: number;
  };
  isMoving: boolean;
  isAttacking: boolean;
}

/**
 * Connected players mapped by user ID
 */
type ConnectedPlayers = Map<string, IConnectedPlayer>;

/**
 * Game state manager
 */
class GameServer {
  private io: SocketIOServer | null = null;
  private connectedPlayers: ConnectedPlayers = new Map();
  private worldState: Partial<IWorld> | null = null;
  private timeUpdateInterval: NodeJS.Timeout | null = null;
  private worldUpdateInterval: NodeJS.Timeout | null = null;

  /**
   * Initialize the game server
   */
  public async init(httpServer: HTTPServer): Promise<void> {
    // Connect to database
    await connectToDatabase();
    
    // Initialize Socket.IO server
    this.io = new SocketIOServer(httpServer, {
      cors: {
        origin: '*',
        methods: ['GET', 'POST'],
      },
    });
    
    // Load world state from database
    await this.loadWorldState();
    
    // Set up event handlers
    this.setupSocketHandlers();
    
    // Start game loops
    this.startTimeUpdateLoop();
    
    console.log('Game server initialized');
  }

  /**
   * Set up socket event handlers
   */
  private setupSocketHandlers(): void {
    if (!this.io) return;

    this.io.on('connection', (socket) => {
      console.log(`New connection: ${socket.id}`);
      
      // Handle player join
      socket.on('auth', async (data: { userId: string }) => {
        try {
          // Load player data from database
          const cat = await Cat.findOne({ owner: data.userId });
          
          // Create player in memory
          const player: IConnectedPlayer = {
            socketId: socket.id,
            cat: cat ? cat.toObject() : null,
            position: cat ? cat.position : { x: 0, y: 0, z: 0, rotation: 0 },
            isMoving: false,
            isAttacking: false,
          };
          
          // Add to connected players
          this.connectedPlayers.set(data.userId, player);
          
          // Save socket data
          socket.data.userId = data.userId;
          
          // Notify other players
          socket.broadcast.emit(SocketEvents.PLAYER_JOIN, {
            playerId: data.userId,
            cat: player.cat,
            position: player.position,
          });
          
          // Send world state to the player
          socket.emit(SocketEvents.WORLD_INIT, {
            world: this.worldState,
            players: this.getPlayersData(),
          });
          
          console.log(`Player authenticated: ${data.userId}`);
        } catch (error) {
          console.error('Authentication error:', error);
          socket.disconnect();
        }
      });
      
      // Handle player movement
      socket.on(SocketEvents.PLAYER_MOVE, (data: { 
        position: { x: number; y: number; z: number; rotation: number }; 
        isMoving: boolean;
      }) => {
        const userId = socket.data.userId;
        if (!userId) return;
        
        const player = this.connectedPlayers.get(userId);
        if (!player) return;
        
        // Update player position
        player.position = data.position;
        player.isMoving = data.isMoving;
        
        // Broadcast to other players
        socket.broadcast.emit(SocketEvents.PLAYER_MOVE, {
          playerId: userId,
          position: data.position,
          isMoving: data.isMoving,
        });
      });
      
      // Handle player attack
      socket.on(SocketEvents.PLAYER_ATTACK, (data: { isAttacking: boolean }) => {
        const userId = socket.data.userId;
        if (!userId) return;
        
        const player = this.connectedPlayers.get(userId);
        if (!player) return;
        
        // Update player state
        player.isAttacking = data.isAttacking;
        
        // Broadcast to other players
        socket.broadcast.emit(SocketEvents.PLAYER_ATTACK, {
          playerId: userId,
          isAttacking: data.isAttacking,
        });
      });
      
      // Handle disconnection
      socket.on('disconnect', () => {
        const userId = socket.data.userId;
        if (!userId) return;
        
        // Remove from connected players
        this.connectedPlayers.delete(userId);
        
        // Notify other players
        this.io?.emit(SocketEvents.PLAYER_LEAVE, {
          playerId: userId,
        });
        
        console.log(`Player disconnected: ${userId}`);
      });
    });
  }

  /**
   * Load world state from database
   */
  private async loadWorldState(): Promise<void> {
    try {
      // Get or create default world
      let world = await World.findOne({});
      
      if (!world) {
        world = await World.create({
          name: 'Cat Annihilation World',
          description: 'A world of warring cats',
          terrain: {
            heightmap: '/assets/terrain/default-heightmap.png',
            size: 2000,
            maxHeight: 100,
          },
          zones: [
            {
              name: 'Starting Area',
              type: 'safe',
              bounds: {
                minX: -100,
                maxX: 100,
                minZ: -100,
                maxZ: 100,
              },
              isPvpAtNight: false,
            },
            {
              name: 'Battle Grounds',
              type: 'pvp',
              bounds: {
                minX: 200,
                maxX: 400,
                minZ: 200,
                maxZ: 400,
              },
              isPvpAtNight: true,
            },
            {
              name: 'Cat Bank',
              type: 'bank',
              bounds: {
                minX: -50,
                maxX: 50,
                minZ: -150,
                maxZ: -50,
              },
              isPvpAtNight: false,
            }
          ],
          dayCycleMinutes: 120,
          nightCycleMinutes: 40,
          currentTime: 0.5, // Start at noon
          isNight: false,
        });
      }
      
      this.worldState = world.toObject();
      console.log('World state loaded');
    } catch (error) {
      console.error('Error loading world state:', error);
    }
  }

  /**
   * Start time update loop
   */
  private startTimeUpdateLoop(): void {
    if (!this.worldState) return;
    
    // Update time every second
    this.timeUpdateInterval = setInterval(() => {
      if (!this.worldState || !this.io) return;
      
      const { dayCycleMinutes, nightCycleMinutes, currentTime, isNight } = this.worldState;
      
      // Calculate time step
      const totalCycleMinutes = isNight ? nightCycleMinutes! : dayCycleMinutes!;
      const timeStep = 1 / (totalCycleMinutes * 60); // Convert minutes to seconds
      
      // Update time
      let newTime = (currentTime! + timeStep) % 1;
      
      // Check if day/night cycle changed
      let newIsNight = isNight;
      if (currentTime! < 0.25 && newTime >= 0.25) {
        // Dawn - Night ends, day begins
        newIsNight = false;
      } else if (currentTime! < 0.75 && newTime >= 0.75) {
        // Dusk - Day ends, night begins
        newIsNight = true;
      }
      
      // Update world state
      this.worldState.currentTime = newTime;
      this.worldState.isNight = newIsNight;
      
      // Broadcast time update to all clients
      this.io.emit(SocketEvents.WORLD_TIME_UPDATE, {
        currentTime: newTime,
        isNight: newIsNight,
      });
      
      // If day/night cycle changed, update database
      if (newIsNight !== isNight) {
        this.updateWorldInDatabase();
        console.log(`Day/night cycle changed: ${newIsNight ? 'Night' : 'Day'}`);
      }
    }, 1000);
  }

  /**
   * Update world state in database
   */
  private async updateWorldInDatabase(): Promise<void> {
    if (!this.worldState) return;
    
    try {
      await World.findOneAndUpdate(
        { name: this.worldState.name },
        {
          currentTime: this.worldState.currentTime,
          isNight: this.worldState.isNight,
        }
      );
    } catch (error) {
      console.error('Error updating world in database:', error);
    }
  }

  /**
   * Get all connected players data for broadcasting
   */
  private getPlayersData(): Record<string, any> {
    const playersData: Record<string, any> = {};
    
    this.connectedPlayers.forEach((player, userId) => {
      playersData[userId] = {
        cat: player.cat,
        position: player.position,
        isMoving: player.isMoving,
        isAttacking: player.isAttacking,
      };
    });
    
    return playersData;
  }

  /**
   * Cleanup resources when shutting down
   */
  public cleanup(): void {
    if (this.timeUpdateInterval) {
      clearInterval(this.timeUpdateInterval);
    }
    
    if (this.worldUpdateInterval) {
      clearInterval(this.worldUpdateInterval);
    }
    
    this.io?.disconnectSockets();
  }
}

// Create a singleton instance
export const gameServer = new GameServer(); 