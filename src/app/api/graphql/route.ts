import { ApolloServer } from '@apollo/server';
import { startServerAndCreateNextHandler } from '@as-integrations/next';
import { NextRequest } from 'next/server';
import { connectToDatabase } from '@/lib/db/connection';
import typeDefs from '@/lib/graphql/schema';
import resolvers from '@/lib/graphql/resolvers';

// Connect to database
connectToDatabase();

// Create Apollo Server
const server = new ApolloServer({
  typeDefs,
  resolvers,
});

// Handle GraphQL requests
const handler = startServerAndCreateNextHandler(server, {
  context: async (req) => ({ req }),
});

// Export the GET and POST handlers for GraphQL API
export async function GET(request: NextRequest) {
  return handler(request);
}

export async function POST(request: NextRequest) {
  return handler(request);
} 