import React from 'react';

/**
 * Universal Error Boundary that prevents any component from unmounting on errors
 * Provides intelligent fallbacks and auto-recovery mechanisms
 */
class UniversalErrorBoundary extends React.Component<
  { 
    children: React.ReactNode;
    componentName?: string;
    fallbackType?: 'scene' | 'character' | 'environment' | 'ui' | 'generic';
    persistOnError?: boolean;
    autoRetrySeconds?: number;
    maxRetries?: number;
  },
  { 
    hasError: boolean;
    errorCount: number;
    lastError?: Error;
    retryAttempt: number;
  }
> {
  private retryTimeout?: NodeJS.Timeout;

  constructor(props: {
    children: React.ReactNode;
    componentName?: string;
    fallbackType?: 'scene' | 'character' | 'environment' | 'ui' | 'generic';
    persistOnError?: boolean;
    autoRetrySeconds?: number;
    maxRetries?: number;
  }) {
    super(props);
    this.state = { 
      hasError: false, 
      errorCount: 0,
      lastError: undefined,
      retryAttempt: 0
    };
  }

  static getDerivedStateFromError(error: Error) {
    return { 
      hasError: true,
      lastError: error
    };
  }

  componentDidCatch(error: Error, errorInfo: React.ErrorInfo) {
    const componentName = this.props.componentName || 'Component';
    console.error(`[UNIVERSAL ERROR BOUNDARY] ${componentName} error:`, error, errorInfo);
    
    this.setState(prevState => ({
      errorCount: prevState.errorCount + 1
    }));

    // Auto-retry logic
    const maxRetries = this.props.maxRetries || 3;
    const autoRetrySeconds = this.props.autoRetrySeconds || 3;
    const persistOnError = this.props.persistOnError !== false; // Default to true

    if (persistOnError && this.state.retryAttempt < maxRetries) {
      console.log(`[UNIVERSAL ERROR BOUNDARY] Auto-retry attempt ${this.state.retryAttempt + 1}/${maxRetries} in ${autoRetrySeconds} seconds...`);
      
      this.retryTimeout = setTimeout(() => {
        console.log(`[UNIVERSAL ERROR BOUNDARY] Attempting to recover ${componentName}...`);
        this.setState(prevState => ({ 
          hasError: false, 
          lastError: undefined,
          retryAttempt: prevState.retryAttempt + 1
        }));
      }, autoRetrySeconds * 1000);
    } else if (this.state.retryAttempt >= maxRetries) {
      console.warn(`[UNIVERSAL ERROR BOUNDARY] ${componentName} reached max retries (${maxRetries}). Showing permanent fallback.`);
    }
  }

  componentWillUnmount() {
    if (this.retryTimeout) {
      clearTimeout(this.retryTimeout);
    }
  }

  render() {
    if (this.state.hasError) {
      const componentName = this.props.componentName || 'Component';
      const fallbackType = this.props.fallbackType || 'generic';
      
      console.log(`[UNIVERSAL ERROR BOUNDARY] Rendering ${fallbackType} fallback for ${componentName}`);
      
      // Provide specific fallbacks based on component type
      switch (fallbackType) {
        case 'scene':
          return (
            <div style={{ 
              width: '100%', 
              height: '100%', 
              display: 'flex', 
              alignItems: 'center', 
              justifyContent: 'center',
              backgroundColor: '#1a1a1a',
              color: '#ffffff',
              fontSize: '18px'
            }}>
              <div style={{ textAlign: 'center' }}>
                <div>🎮 Scene Temporarily Unavailable</div>
                <div style={{ fontSize: '14px', marginTop: '10px', opacity: 0.7 }}>
                  {this.state.retryAttempt < (this.props.maxRetries || 3) ? 
                    'Attempting automatic recovery...' : 
                    'Please refresh the page to restore full functionality'
                  }
                </div>
              </div>
            </div>
          );

        case 'character':
          return (
            <mesh position={[0, 0.5, 0]}>
              <boxGeometry args={[1, 1, 1]} />
              <meshStandardMaterial color="#ffa500" />
            </mesh>
          );

        case 'environment':
          return (
            <group>
              <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, -0.1, 0]} receiveShadow>
                <planeGeometry args={[200, 200]} />
                <meshStandardMaterial color="#7fb069" />
              </mesh>
              <fog attach="fog" args={['#87CEEB', 30, 150]} />
            </group>
          );

        case 'ui':
          return (
            <div style={{ 
              padding: '10px', 
              backgroundColor: 'rgba(255, 0, 0, 0.1)', 
              border: '1px solid #ff6666',
              borderRadius: '4px',
              color: '#ff6666',
              fontSize: '12px'
            }}>
              UI Component Error - {componentName}
            </div>
          );

        case 'generic':
        default:
          return (
            <group>
              <mesh position={[0, 0, 0]} visible={false}>
                <boxGeometry args={[0.1, 0.1, 0.1]} />
                <meshBasicMaterial />
              </mesh>
            </group>
          );
      }
    }

    return this.props.children;
  }
}

export default UniversalErrorBoundary;