/* Copyright (C) 2016, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ingame_mode.hpp"

#include "data/game_traits.hpp"
#include "data/map.hpp"
#include "data/sound_ids.hpp"
#include "engine/debugging_system.hpp"
#include "engine/physics_system.hpp"
#include "engine/rendering_system.hpp"
#include "game_logic/ai/security_camera.hpp"
#include "game_logic/damage_infliction_system.hpp"
#include "game_logic/interaction/elevator.hpp"
#include "game_logic/map_scroll_system.hpp"
#include "game_logic/player/animation_system.hpp"
#include "game_logic/player/attack_system.hpp"
#include "game_logic/player/damage_system.hpp"
#include "game_logic/player_interaction_system.hpp"
#include "game_logic/player_movement_system.hpp"
#include "game_logic/trigger_components.hpp"
#include "loader/resource_loader.hpp"
#include "ui/utils.hpp"

#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>


namespace rigel {

using namespace engine;
using namespace game_logic;
using namespace std;

using data::PlayerModel;
using engine::components::BoundingBox;
using engine::components::Physical;
using engine::components::WorldPosition;


namespace {

char EPISODE_PREFIXES[] = {'L', 'M', 'N', 'O'};

std::string levelFileName(const int episode, const int level) {
  assert(episode >=0 && episode < 4);
  assert(level >=0 && level < 8);

  std::string fileName;
  fileName += EPISODE_PREFIXES[episode];
  fileName += std::to_string(level + 1);
  fileName += ".MNI";
  return fileName;
}


std::string loadingScreenFileName(const int episode) {
  assert(episode >=0 && episode < 4);

  std::string fileName("LOAD");
  fileName += std::to_string(episode + 1);
  fileName += ".MNI";
  return fileName;
}


template<typename ValueT>
std::string vec2String(const base::Point<ValueT>& vec, const int width) {
  std::stringstream stream;
  stream
    << std::setw(width) << std::fixed << std::setprecision(2) << vec.x << ", "
    << std::setw(width) << std::fixed << std::setprecision(2) << vec.y;
  return stream.str();
}

}


IngameMode::IngameMode(
  const int episode,
  const int levelNumber,
  const data::Difficulty difficulty,
  Context context,
  boost::optional<base::Vector> playerPositionOverride
)
  : mpRenderer(context.mpRenderer)
  , mpServiceProvider(context.mpServiceProvider)
  , mEntityFactory(
      context.mpRenderer,
      &mEntities.entities,
      &context.mpResources->mActorImagePackage,
      difficulty)
  , mPlayerModelAtLevelStart(mPlayerModel)
  , mLevelFinished(false)
  , mShowDebugText(false)
  , mHudRenderer(
      &mPlayerModel,
      levelNumber + 1,
      mpRenderer,
      *context.mpResources)
  , mIngameViewPortRenderTarget(
      context.mpRenderer,
      data::GameTraits::inGameViewPortSize.width,
      data::GameTraits::inGameViewPortSize.height)
{
  showLoadingScreen(episode, *context.mpResources);

  using namespace std::chrono;
  auto before = high_resolution_clock::now();

  loadLevel(episode, levelNumber, difficulty, *context.mpResources);

  if (playerPositionOverride) {
    *mPlayerEntity.component<WorldPosition>().get() = *playerPositionOverride;
  }

  auto after = high_resolution_clock::now();
  std::cout << "Level load time: " <<
    duration<double>(after - before).count() * 1000.0 << " ms\n";
}


void IngameMode::handleEvent(const SDL_Event& event) {
  if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) {
    return;
  }

  const auto keyPressed = event.type == SDL_KEYDOWN;
  switch (event.key.keysym.sym) {
    case SDLK_UP:
      mPlayerInputs.mMovingUp = keyPressed;
      break;
    case SDLK_DOWN:
      mPlayerInputs.mMovingDown = keyPressed;
      break;
    case SDLK_LEFT:
      mPlayerInputs.mMovingLeft = keyPressed;
      break;
    case SDLK_RIGHT:
      mPlayerInputs.mMovingRight = keyPressed;
      break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
      mPlayerInputs.mJumping = keyPressed;
      break;
    case SDLK_LALT:
    case SDLK_RALT:
      mPlayerInputs.mShooting = keyPressed;
      break;
  }

  // Debug keys
  // ----------------------------------------------------------------------
  if (keyPressed) {
    return;
  }

  auto& debuggingSystem = *mEntities.systems.system<DebuggingSystem>();
  switch (event.key.keysym.sym) {
    case SDLK_b:
      debuggingSystem.toggleBoundingBoxDisplay();
      break;

    case SDLK_c:
      debuggingSystem.toggleWorldCollisionDataDisplay();
      break;

    case SDLK_d:
      mShowDebugText = !mShowDebugText;
      break;
  }
}


void IngameMode::updateAndRender(engine::TimeDelta dt) {
  if (mLevelFinished) {
    return;
  }

  // **********************************************************************
  // Updating
  // **********************************************************************
  mEntities.systems.system<player::AttackSystem>()->setInputState(
    mPlayerInputs);
  mEntities.systems.system<interaction::ElevatorSystem>()->setInputState(
    mPlayerInputs);

  // ----------------------------------------------------------------------
  // Player logic update
  // ----------------------------------------------------------------------
  // TODO: Move all player related systems into the player namespace
  mEntities.systems.update<interaction::ElevatorSystem>(dt);

  mEntities.systems.update<PlayerMovementSystem>(dt);
  mEntities.systems.update<player::AttackSystem>(dt);
  mEntities.systems.update<PlayerInteractionSystem>(dt);

  // ----------------------------------------------------------------------
  // A.I. logic update
  // ----------------------------------------------------------------------
  mEntities.systems.update<ai::SecurityCameraSystem>(dt);

  // ----------------------------------------------------------------------
  // Physics and other updates
  // ----------------------------------------------------------------------
  mEntities.systems.update<PhysicsSystem>(dt);

  mEntities.systems.update<player::DamageSystem>(dt);
  mEntities.systems.update<DamageInflictionSystem>(dt);
  mEntities.systems.update<player::AnimationSystem>(dt);
  mEntities.systems.update<MapScrollSystem>(dt);

  // **********************************************************************
  // Rendering
  // **********************************************************************
  {
    sdl_utils::RenderTargetTexture::Binder
      bindRenderTarget(mIngameViewPortRenderTarget, mpRenderer);

    mEntities.systems.update<RenderingSystem>(dt);
    mEntities.systems.update<DebuggingSystem>(dt);
    mHudRenderer.updateAndRender(dt);
  }

  mIngameViewPortRenderTarget.render(
    mpRenderer,
    data::GameTraits::inGameViewPortOffset.x,
    data::GameTraits::inGameViewPortOffset.y);

  if (mShowDebugText) {
    showDebugText();
  }

  checkForPlayerDeath();
  checkForLevelExitReached();
}


bool IngameMode::levelFinished() const {
  return mLevelFinished;
}


void IngameMode::showLoadingScreen(
  const int episode,
  const loader::ResourceLoader& resources
) {
  mpServiceProvider->fadeOutScreen();
  mpServiceProvider->playMusic("MENUSNG2.IMF");
  {
    const auto loadingScreenTexture = ui::fullScreenImageAsTexture(
      mpRenderer,
      resources,
      loadingScreenFileName(episode));
    loadingScreenTexture.render(mpRenderer, 0, 0);
  }
  mpServiceProvider->fadeInScreen();
}


void IngameMode::loadLevel(
  const int episode,
  const int levelNumber,
  const data::Difficulty difficulty,
  const loader::ResourceLoader& resources
) {
  auto loadedLevel = loader::loadLevel(
    levelFileName(episode, levelNumber), resources, difficulty);
  mPlayerEntity = mEntityFactory.createEntitiesForLevel(loadedLevel.mActors);

  mLevelData = LevelData{
    std::move(loadedLevel.mMap),
    std::move(loadedLevel.mTileSet.mAttributes),
    std::move(loadedLevel.mActors)
  };
  mMapAtLevelStart = mLevelData.mMap;

  mEntities.systems.add<PhysicsSystem>(
    &mLevelData.mMap,
    &mLevelData.mTileAttributes);
  mEntities.systems.add<game_logic::PlayerMovementSystem>(
    mPlayerEntity,
    &mPlayerInputs,
    mLevelData.mMap,
    mLevelData.mTileAttributes);
  mEntities.systems.add<game_logic::player::AnimationSystem>(
    mPlayerEntity,
    mpServiceProvider,
    &mEntityFactory);
  mEntities.systems.add<game_logic::player::AttackSystem>(
    mPlayerEntity,
    &mPlayerModel,
    mpServiceProvider,
    [this](
      const game_logic::ProjectileType type,
      const WorldPosition& pos,
      const game_logic::ProjectileDirection direction
    ) {
      mEntityFactory.createProjectile(type, pos, direction);
    });
  mEntities.systems.add<game_logic::player::DamageSystem>(
    mPlayerEntity,
    &mPlayerModel,
    mpServiceProvider,
    difficulty);
  mEntities.systems.add<game_logic::MapScrollSystem>(
    &mScrollOffset,
    mPlayerEntity,
    mLevelData.mMap);
  mEntities.systems.add<RenderingSystem>(
    &mScrollOffset,
    mpRenderer,
    &mLevelData.mMap,
    &mLevelData.mTileAttributes,
    std::move(loadedLevel.mTileSet.mImage),
    std::move(loadedLevel.mBackdropImage),
    std::move(loadedLevel.mSecondaryBackdropImage),
    loadedLevel.mBackdropScrollMode);
  mEntities.systems.add<PlayerInteractionSystem>(
    mPlayerEntity,
    &mPlayerModel,
    mpServiceProvider);
  mEntities.systems.add<DamageInflictionSystem>(
    &mPlayerModel,
    &mLevelData.mMap,
    mpServiceProvider);
  mEntities.systems.add<ai::SecurityCameraSystem>(mPlayerEntity);
  mEntities.systems.add<DebuggingSystem>(
    mpRenderer,
    &mScrollOffset,
    &mLevelData.mMap,
    &mLevelData.mTileAttributes);
  mEntities.systems.add<interaction::ElevatorSystem>(mPlayerEntity);
  mEntities.systems.configure();

  mpServiceProvider->playMusic(loadedLevel.mMusicFile);
}


void IngameMode::checkForLevelExitReached() {
  using game_logic::components::Trigger;
  using game_logic::components::TriggerType;

  mEntities.entities.each<Trigger, WorldPosition>(
    [this](
      entityx::Entity,
      const Trigger& trigger,
      const WorldPosition& triggerPosition
    ) {
      if (trigger.mType != TriggerType::LevelExit || mLevelFinished) {
        return;
      }

      const auto& playerPosition =
        *mPlayerEntity.component<WorldPosition>().get();
      const auto playerBBox = toWorldSpace(
        *mPlayerEntity.component<BoundingBox>().get(), playerPosition);

      const auto playerAboveOrAtTriggerHeight =
        playerBBox.bottom() <= triggerPosition.y;
      const auto touchingTriggerOnXAxis =
        triggerPosition.x >= playerBBox.left() &&
        triggerPosition.x <= (playerBBox.right() + 1);

      // TODO: Add check for trigger being visible on-screen to properly
      // replicate the original game's behavior

      mLevelFinished = playerAboveOrAtTriggerHeight && touchingTriggerOnXAxis;
    });
}


void IngameMode::checkForPlayerDeath() {
  const auto& playerState =
    *mPlayerEntity.component<game_logic::components::PlayerControlled>().get();

  const auto playerDead =
    playerState.mState == player::PlayerState::Dead &&
    mPlayerModel.mHealth <= 0;
  if (playerDead) {
    restartLevel();
  }
}


void IngameMode::restartLevel() {
  mpServiceProvider->fadeOutScreen();

  mLevelData.mMap = mMapAtLevelStart;

  mEntities.entities.reset();
  mPlayerEntity = mEntityFactory.createEntitiesForLevel(
    mLevelData.mInitialActors);

  mPlayerModel = mPlayerModelAtLevelStart;

  updateAndRender(0);

  mpServiceProvider->fadeInScreen();
}


void IngameMode::showDebugText() {
  const auto& playerPos = *mPlayerEntity.component<WorldPosition>().get();
  const auto& playerVel = mPlayerEntity.component<Physical>()->mVelocity;
  std::stringstream infoText;
  infoText
    << "Scroll: " << vec2String(mScrollOffset, 4) << '\n'
    << "Player: " << vec2String(playerPos, 4)
    << ", Vel.: " << vec2String(playerVel, 5);

  mpServiceProvider->showDebugText(infoText.str());
}

}
