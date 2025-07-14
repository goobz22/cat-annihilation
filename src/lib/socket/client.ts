import { io, Socket } from 'socket.io-client';
import { useGameStore } from '@/lib/store/gameStore';

/**
 * Socket event types
 */
export enum SocketEvents {
  // Connection events
  CONNECT = 'connect',
  DISCONNECT = 'disconnect',
  CONNECT_ERROR = 'connect_error',
  
  // Player events
  PLAYER_JOIN = 'player:join',
  PLAYER_LEAVE = 'player:leave',
  PLAYER_MOVE = 'player:move',
  PLAYER_ATTACK = 'player:attack',
  
  // World events
  WORLD_INIT = 'world:init',
  WORLD_UPDATE = 'world:update',
  WORLD_TIME_UPDATE = 'world:timeUpdate',
  
  // Game events
  GAME_COMBAT = 'game:combat',
  GAME_ITEM_DROP = 'game:itemDrop',
  GAME_ITEM_PICKUP = 'game:itemPickup',
}

/**
 * Socket client instance
 */
class SocketClient {
  private socket: Socket | null = null;
  private initialized = false;

  /**
   * Connect to the game server
   */
  public connect(): void {
    // Call init to make sure we're connected
    this.init();
  }

  /**
   * Initialize socket connection
   */
  public init(): void {
    if (this.initialized) return;
    
    const socketUrl = process.env.NEXT_PUBLIC_SOCKET_URL || 'http://localhost:3000';
    
    try {
      this.socket = io(socketUrl, {
        transports: ['websocket'],
        autoConnect: true,
      });

      this.setupEventListeners();
      this.initialized = true;
      console.log('Socket client initialized');
    } catch (error) {
      console.error('Socket initialization error:', error);
      useGameStore.getState().setConnectionState(false, 'Failed to connect to game server');
    }
  }

  /**
   * Set up socket event listeners
   */
  private setupEventListeners(): void {
    if (!this.socket) return;

    // Connection events
    this.socket.on(SocketEvents.CONNECT, this.handleConnect);
    this.socket.on(SocketEvents.DISCONNECT, this.handleDisconnect);
    this.socket.on(SocketEvents.CONNECT_ERROR, this.handleConnectError);

    // Player events
    this.socket.on(SocketEvents.PLAYER_JOIN, this.handlePlayerJoin);
    this.socket.on(SocketEvents.PLAYER_LEAVE, this.handlePlayerLeave);
    this.socket.on(SocketEvents.PLAYER_MOVE, this.handlePlayerMove);
    this.socket.on(SocketEvents.PLAYER_ATTACK, this.handlePlayerAttack);

    // World events
    this.socket.on(SocketEvents.WORLD_INIT, this.handleWorldInit);
    this.socket.on(SocketEvents.WORLD_UPDATE, this.handleWorldUpdate);
    this.socket.on(SocketEvents.WORLD_TIME_UPDATE, this.handleWorldTimeUpdate);
  }

  /**
   * Handle socket connection
   */
  private handleConnect = (): void => {
    // Only update connection state if it's not already connected
    if (!useGameStore.getState().isConnected) {
      useGameStore.getState().setConnectionState(true);
      console.log('Connected to game server');
    }
  };

  /**
   * Handle socket disconnection
   */
  private handleDisconnect = (reason: string): void => {
    useGameStore.getState().setConnectionState(false, `Disconnected: ${reason}`);
    console.log(`Disconnected from game server: ${reason}`);
  };

  /**
   * Handle socket connection error
   */
  private handleConnectError = (error: Error): void => {
    useGameStore.getState().setConnectionState(false, `Connection error: ${error.message}`);
    console.error('Connection error:', error);
  };

  /**
   * Handle player join event
   */
  private handlePlayerJoin = (data: { playerId: string; cat: any; position: any }): void => {
    useGameStore.getState().updateOtherPlayer(data.playerId, {
      cat: data.cat,
      position: data.position,
    });
    console.log(`Player joined: ${data.playerId}`);
  };

  /**
   * Handle player leave event
   */
  private handlePlayerLeave = (data: { playerId: string }): void => {
    useGameStore.getState().removeOtherPlayer(data.playerId);
    console.log(`Player left: ${data.playerId}`);
  };

  /**
   * Handle player move event
   */
  private handlePlayerMove = (data: { 
    playerId: string; 
    position: { x: number; y: number; z: number; rotation: number }; 
    isMoving: boolean;
  }): void => {
    useGameStore.getState().updateOtherPlayer(data.playerId, {
      position: data.position,
      isMoving: data.isMoving,
    });
  };

  /**
   * Handle player attack event
   */
  private handlePlayerAttack = (data: { 
    playerId: string; 
    isAttacking: boolean; 
  }): void => {
    useGameStore.getState().updateOtherPlayer(data.playerId, {
      isAttacking: data.isAttacking,
    });
  };

  /**
   * Handle world initialization
   */
  private handleWorldInit = (data: { world: any; players: Record<string, any> }): void => {
    useGameStore.getState().setWorld(data.world);
    
    // Add all existing players
    Object.entries(data.players).forEach(([playerId, playerData]) => {
      useGameStore.getState().updateOtherPlayer(playerId, playerData);
    });
    
    console.log('World initialized');
  };

  /**
   * Handle world update
   */
  private handleWorldUpdate = (data: { world: any }): void => {
    useGameStore.getState().setWorld(data.world);
  };

  /**
   * Handle world time update
   */
  private handleWorldTimeUpdate = (data: { 
    currentTime: number; 
    isNight: boolean;
  }): void => {
    useGameStore.getState().setDayCycle({
      currentTime: data.currentTime,
      isNight: data.isNight,
    });
  };

  /**
   * Emit player movement
   */
  public emitPlayerMove(position: { x: number; y: number; z: number; rotation: number }, isMoving: boolean): void {
    if (!this.socket) return;
    
    this.socket.emit(SocketEvents.PLAYER_MOVE, {
      position,
      isMoving,
    });
  }

  /**
   * Emit player attack
   */
  public emitPlayerAttack(isAttacking: boolean): void {
    if (!this.socket) return;
    
    this.socket.emit(SocketEvents.PLAYER_ATTACK, {
      isAttacking,
    });
  }

  /**
   * Disconnect socket
   */
  public disconnect(): void {
    if (!this.socket) return;
    
    this.socket.disconnect();
    this.socket = null;
    this.initialized = false;
  }
}

// Create a singleton instance
export const socketClient = new SocketClient(); 