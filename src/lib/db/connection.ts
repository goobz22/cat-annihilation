import mongoose from 'mongoose';

/**
 * Global variable to track the connection status
 */
let isConnected = false;

/**
 * Connects to MongoDB database
 * Uses a cached connection if already established
 */
export const connectToDatabase = async (): Promise<void> => {
  if (isConnected) {
    console.log('Using existing database connection');
    return;
  }

  try {
    const db = await mongoose.connect(process.env.MONGODB_URI || 'mongodb://localhost:27017/cat-annihilation');
    isConnected = !!db.connections[0].readyState;
    console.log('New database connection established');
  } catch (error) {
    console.error('Error connecting to database:', error);
    throw error;
  }
};

/**
 * Closes the MongoDB connection
 */
export const disconnectFromDatabase = async (): Promise<void> => {
  if (!isConnected) {
    return;
  }

  try {
    await mongoose.disconnect();
    isConnected = false;
    console.log('Database connection closed');
  } catch (error) {
    console.error('Error closing database connection:', error);
    throw error;
  }
}; 