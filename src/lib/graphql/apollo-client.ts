import { ApolloClient, InMemoryCache, NormalizedCacheObject } from '@apollo/client';

/**
 * Client-side Apollo client
 */
let apolloClient: ApolloClient<NormalizedCacheObject> | undefined;

/**
 * Create a new Apollo client
 */
export function createApolloClient() {
  return new ApolloClient({
    ssrMode: typeof window === 'undefined',
    uri: process.env.NEXT_PUBLIC_API_URL + '/graphql' || 'http://localhost:3000/api/graphql',
    cache: new InMemoryCache(),
    defaultOptions: {
      watchQuery: {
        fetchPolicy: 'cache-and-network',
      },
    },
  });
}

/**
 * Get or create Apollo client instance
 */
export function initializeApollo(initialState: any = null) {
  const _apolloClient = apolloClient ?? createApolloClient();

  // If your page has Next.js data fetching methods that use Apollo Client,
  // the initial state gets hydrated here
  if (initialState) {
    // Get existing cache, loaded during client side data fetching
    const existingCache = _apolloClient.extract();

    // Merge the existing cache into data passed from getStaticProps/getServerSideProps
    const data = {
      ...existingCache,
      ...initialState,
    };

    // Restore the cache with the merged data
    _apolloClient.cache.restore(data);
  }

  // For SSG and SSR always create a new Apollo Client
  if (typeof window === 'undefined') return _apolloClient;

  // Create the Apollo Client once in the client
  if (!apolloClient) apolloClient = _apolloClient;

  return _apolloClient;
}

/**
 * Use this function to get the Apollo client in your components
 */
export function useApollo(initialState: any) {
  return initializeApollo(initialState);
} 