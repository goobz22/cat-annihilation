import { useEffect, useState } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const QuestBook = () => {
  const questBookOpen = useGameStore(state => state.storyMode.questBookOpen);
  const quests = useGameStore(state => state.storyMode.quests);
  const activeQuests = useGameStore(state => state.storyMode.activeQuests);
  const completedQuests = useGameStore(state => state.storyMode.completedQuests);
  const playerRank = useGameStore(state => state.storyMode.playerRank);
  const playerClan = useGameStore(state => state.storyMode.playerClan);
  const storySkills = useGameStore(state => state.storyMode.storySkills);
  const storyModeActive = useGameStore(state => state.storyMode.isActive);
  
  const toggleQuestBook = useGameStore(state => state.toggleQuestBook);
  const activateQuest = useGameStore(state => state.activateQuest);
  const setMenuPaused = useGameStore(state => state.setMenuPaused);

  const [selectedCategory, setSelectedCategory] = useState<'available' | 'active' | 'completed'>('active');
  const [selectedQuest, setSelectedQuest] = useState<string | null>(null);

  useEffect(() => {
    const handleKeyPress = (e: KeyboardEvent) => {
      if (e.key === 'q' || e.key === 'Q') {
        e.preventDefault();
        toggleQuestBook();
      }
      
      if (questBookOpen && e.key === 'Escape') {
        toggleQuestBook();
      }
    };

    window.addEventListener('keydown', handleKeyPress);
    return () => window.removeEventListener('keydown', handleKeyPress);
  }, [questBookOpen, toggleQuestBook]);

  // Pause/unpause game when quest book opens/closes
  useEffect(() => {
    setMenuPaused(questBookOpen);
  }, [questBookOpen, setMenuPaused]);

  const getQuestsByCategory = (category: 'available' | 'active' | 'completed') => {
    switch (category) {
      case 'available':
        return quests.filter(quest => quest.status === 'available');
      case 'active': 
        return quests.filter(quest => quest.status === 'active');
      case 'completed':
        return quests.filter(quest => quest.status === 'completed');
      default:
        return [];
    }
  };

  const getCategoryCount = (category: 'available' | 'active' | 'completed') => {
    return getQuestsByCategory(category).length;
  };

  const handleQuestSelect = (questId: string) => {
    setSelectedQuest(selectedQuest === questId ? null : questId);
  };

  const handleQuestActivate = (questId: string) => {
    const quest = quests.find(q => q.id === questId);
    if (quest && quest.status === 'available') {
      activateQuest(questId);
      console.log(`📋 Activated quest: ${quest.title}`);
    }
  };

  const selectedQuestData = selectedQuest ? quests.find(q => q.id === selectedQuest) : null;

  // Only show quest book if story mode is active (after all hooks are called)
  if (!storyModeActive) return null;

  if (!questBookOpen) return null;

  return (
    <div className="quest-book-overlay">
      <div className="quest-book-container">
        {/* Header */}
        <div className="quest-book-header">
          <div className="quest-book-title">
            <h1>📜 Quest Chronicle</h1>
            <div className="player-info">
              <span className="player-clan">{playerClan}</span>
              <span className="player-rank">{playerRank.replace('_', ' ')}</span>
            </div>
          </div>
          <button 
            className="quest-book-close"
            onClick={toggleQuestBook}
          >
            ✕
          </button>
        </div>

        <div className="quest-book-content">
          {/* Left Panel - Categories and Quest List */}
          <div className="quest-book-left">
            {/* Category Tabs */}
            <div className="quest-categories">
              <button 
                className={`quest-category ${selectedCategory === 'active' ? 'active' : ''}`}
                onClick={() => setSelectedCategory('active')}
              >
                🔥 Active ({getCategoryCount('active')})
              </button>
              <button 
                className={`quest-category ${selectedCategory === 'available' ? 'active' : ''}`}
                onClick={() => setSelectedCategory('available')}
              >
                ⭐ Available ({getCategoryCount('available')})
              </button>
              <button 
                className={`quest-category ${selectedCategory === 'completed' ? 'active' : ''}`}
                onClick={() => setSelectedCategory('completed')}
              >
                ✅ Completed ({getCategoryCount('completed')})
              </button>
            </div>

            {/* Quest List */}
            <div className="quest-list">
              {getQuestsByCategory(selectedCategory).map(quest => (
                <div 
                  key={quest.id}
                  className={`quest-item ${selectedQuest === quest.id ? 'selected' : ''} ${quest.category}`}
                  onClick={() => handleQuestSelect(quest.id)}
                >
                  <div className="quest-item-header">
                    <span className="quest-title">{quest.title}</span>
                    <span className="quest-category-badge">{quest.category}</span>
                  </div>
                  <div className="quest-summary">{quest.description}</div>
                  {quest.status === 'active' && (
                    <div className="quest-progress">
                      {quest.objectives.map(obj => (
                        <div key={obj.id} className="objective-progress">
                          <span>{obj.description}</span>
                          <span className="progress-count">
                            {obj.currentCount}/{obj.count}
                          </span>
                        </div>
                      ))}
                    </div>
                  )}
                </div>
              ))}
              
              {getQuestsByCategory(selectedCategory).length === 0 && (
                <div className="no-quests">
                  {selectedCategory === 'active' && "No active quests. Select an available quest to begin!"}
                  {selectedCategory === 'available' && "No available quests. Complete current quests to unlock more!"}
                  {selectedCategory === 'completed' && "No completed quests yet. Start your journey!"}
                </div>
              )}
            </div>
          </div>

          {/* Right Panel - Quest Details */}
          <div className="quest-book-right">
            {selectedQuestData ? (
              <div className="quest-details">
                <div className="quest-details-header">
                  <h3>{selectedQuestData.title}</h3>
                  <span className={`quest-status ${selectedQuestData.status}`}>
                    {selectedQuestData.status}
                  </span>
                </div>

                <div className="quest-details-body">
                  <div className="quest-description">
                    <h4>Description</h4>
                    <p>{selectedQuestData.description}</p>
                  </div>

                  {selectedQuestData.giver && (
                    <div className="quest-giver">
                      <h4>Quest Giver</h4>
                      <p>{selectedQuestData.giver}</p>
                    </div>
                  )}

                  {selectedQuestData.location && (
                    <div className="quest-location">
                      <h4>Location</h4>
                      <p>{selectedQuestData.location}</p>
                    </div>
                  )}

                  <div className="quest-objectives">
                    <h4>Objectives</h4>
                    {selectedQuestData.objectives.map(obj => (
                      <div key={obj.id} className="objective">
                        <div className="objective-text">
                          {obj.description}
                        </div>
                        <div className="objective-progress-bar">
                          <div 
                            className="progress-fill"
                            style={{ width: `${(obj.currentCount / obj.count) * 100}%` }}
                          />
                          <span className="progress-text">
                            {obj.currentCount}/{obj.count}
                          </span>
                        </div>
                      </div>
                    ))}
                  </div>

                  <div className="quest-rewards">
                    <h4>Rewards</h4>
                    {selectedQuestData.rewards.map((reward, index) => (
                      <div key={index} className="reward">
                        {reward.description}
                      </div>
                    ))}
                  </div>

                  {selectedQuestData.status === 'available' && (
                    <div className="quest-actions">
                      <button 
                        className="activate-quest-btn"
                        onClick={() => handleQuestActivate(selectedQuestData.id)}
                      >
                        Activate Quest
                      </button>
                    </div>
                  )}
                </div>
              </div>
            ) : (
              <div className="no-quest-selected">
                <h3>Select a quest to view details</h3>
                <p>Choose a quest from the list to see objectives, rewards, and progress.</p>
                
                {/* Story Skills Summary */}
                <div className="skills-summary">
                  <h4>Your Skills</h4>
                  <div className="skills-grid">
                    {Object.entries(storySkills).map(([skill, xp]) => (
                      <div key={skill} className="skill-item">
                        <span className="skill-name">{skill}</span>
                        <span className="skill-xp">{xp} XP</span>
                      </div>
                    ))}
                  </div>
                </div>
              </div>
            )}
          </div>
        </div>

        {/* Footer */}
        <div className="quest-book-footer">
          <div className="controls-hint">
            Press <kbd>Q</kbd> to toggle Quest Book • Press <kbd>ESC</kbd> to close
          </div>
        </div>
      </div>
    </div>
  );
};

export default QuestBook;