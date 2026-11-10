# plus.1 (fork)

- Objects the simulator has no hardcoded entry for (2.2 blocks/spikes) are now read from the game's real object data instead of being silently dropped
- Fixed typo in tryInputs (lastX used pos.y)
- Search stops after 90s without progress and exports the partial macro instead of hanging
- Warns when the level contains mechanics the simulator cannot model (dual, robot, spider, swing, teleport, dash orbs, triggers)
- Empty-level crash guard

