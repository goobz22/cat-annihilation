import { useGameStore } from '../../lib/store/gameStore';

/**
 * QuestObjectiveOverlay shows active quest objectives on screen
 * Displays current quest progress in a compact overlay
 */
const QuestObjectiveOverlay = () => {
  const storyModeActive = useGameStore(state => state.storyMode.isActive);
  const activeQuests = useGameStore(state => state.storyMode.activeQuests);
  const quests = useGameStore(state => state.storyMode.quests);
  const playerRank = useGameStore(state => state.storyMode.playerRank);
  const playerClan = useGameStore(state => state.storyMode.playerClan);

  // Only show if story mode is active and there are active quests
  if (!storyModeActive || activeQuests.length === 0) return null;

  const activeQuestData = activeQuests
    .map(questId => quests.find(q => q.id === questId))
    .filter(quest => quest && quest.status === 'active');

  if (activeQuestData.length === 0) return null;

  // Show only the first active quest to avoid clutter
  const currentQuest = activeQuestData[0];
  if (!currentQuest) return null;

  const getObjectiveIcon = (type: string) => {
    switch (type) {
      case 'visit': return '🗺️';
      case 'kill': return '⚔️';
      case 'collect': return '📦';
      case 'talk': return '💬';
      case 'survive': return '🛡️';
      case 'escort': return '🚶';
      default: return '📋';
    }
  };

  const getRankColor = (rank: string) => {
    switch (rank) {
      case 'outsider': return '#888';
      case 'apprentice': return '#4ecdc4';
      case 'warrior': return '#ffd700';
      case 'senior_warrior': return '#ff8c42';
      case 'deputy': return '#e74c3c';
      case 'leader': return '#9b59b6';
      default: return '#888';
    }
  };

  return (
    <div className="quest-objective-overlay">
      {/* Player Status */}
      <div className="player-status">
        <div className="clan-name">{playerClan}</div>
        <div 
          className="player-rank"
          style={{ color: getRankColor(playerRank) }}
        >
          {playerRank.replace('_', ' ').toUpperCase()}
        </div>
      </div>

      {/* Current Quest */}
      <div className="current-quest">
        <div className="quest-header">
          <div className="quest-icon">📜</div>
          <div className="quest-info">
            <div className="quest-title">{currentQuest.title}</div>
            <div className="quest-category">{currentQuest.category} quest</div>
          </div>
        </div>

        {/* Objectives */}
        <div className="quest-objectives">
          {currentQuest.objectives.map(objective => {
            const isCompleted = objective.currentCount >= objective.count;
            const progress = Math.min(objective.currentCount, objective.count);
            const progressPercent = (progress / objective.count) * 100;

            return (
              <div 
                key={objective.id} 
                className={`objective ${isCompleted ? 'completed' : ''}`}
              >
                <div className="objective-header">
                  <span className="objective-icon">
                    {getObjectiveIcon(objective.type)}
                  </span>
                  <span className="objective-text">
                    {objective.description}
                  </span>
                  <span className="objective-counter">
                    {progress}/{objective.count}
                  </span>
                </div>
                
                <div className="objective-progress">
                  <div 
                    className="progress-bar"
                    style={{ width: `${progressPercent}%` }}
                  />
                </div>
              </div>
            );
          })}
        </div>

        {/* Quest Book Hint */}
        <div className="quest-hint">
          Press <kbd>Q</kbd> for Quest Book
        </div>
      </div>
    </div>
  );
};

export default QuestObjectiveOverlay;