#pragma once

#include "cat_customization.hpp"
#include <map>

namespace CatGame {

/**
 * Static accessory database
 * Contains all available cat accessories with their properties
 */
class AccessoryData {
public:
    static std::map<std::string, CatAccessory> getAllAccessories() {
        std::map<std::string, CatAccessory> accessories;

        // ====================================================================
        // HEAD ACCESSORIES
        // ====================================================================

        {
            CatAccessory crown;
            crown.id = "crown_leader";
            crown.name = "Leader's Crown";
            crown.description = "A golden crown for the leader of the clan";
            crown.slot = AccessorySlot::Head;
            crown.modelPath = "assets/models/accessories/crown.glb";
            crown.positionOffset = glm::vec3(0.0f, 0.05f, 0.0f);
            crown.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            crown.scale = 0.8f;
            crown.isUnlocked = false;
            crown.unlockCondition = "rank_leader";
            crown.unlockLevel = 20;
            crown.tintColor = glm::vec3(1.0f, 0.84f, 0.0f); // Gold
            crown.allowColorCustomization = false;
            crown.metallic = 0.9f;
            crown.roughness = 0.2f;
            accessories[crown.id] = crown;
        }

        {
            CatAccessory flowerCrown;
            flowerCrown.id = "crown_flower";
            flowerCrown.name = "Flower Crown";
            flowerCrown.description = "A beautiful crown of spring flowers";
            flowerCrown.slot = AccessorySlot::Head;
            flowerCrown.modelPath = "assets/models/accessories/flower_crown.glb";
            flowerCrown.positionOffset = glm::vec3(0.0f, 0.03f, 0.0f);
            flowerCrown.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            flowerCrown.scale = 0.9f;
            flowerCrown.isUnlocked = false;
            flowerCrown.unlockCondition = "event_spring";
            flowerCrown.unlockLevel = 5;
            flowerCrown.tintColor = glm::vec3(1.0f, 0.7f, 0.8f); // Pink
            flowerCrown.allowColorCustomization = true;
            flowerCrown.metallic = 0.0f;
            flowerCrown.roughness = 0.8f;
            accessories[flowerCrown.id] = flowerCrown;
        }

        {
            CatAccessory battleHelm;
            battleHelm.id = "helm_battle";
            battleHelm.name = "Battle Helmet";
            battleHelm.description = "Forged in the fires of combat";
            battleHelm.slot = AccessorySlot::Head;
            battleHelm.modelPath = "assets/models/accessories/battle_helm.glb";
            battleHelm.positionOffset = glm::vec3(0.0f, 0.02f, 0.0f);
            battleHelm.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            battleHelm.scale = 0.85f;
            battleHelm.isUnlocked = false;
            battleHelm.unlockCondition = "rank_warrior";
            battleHelm.unlockLevel = 15;
            battleHelm.tintColor = glm::vec3(0.7f, 0.7f, 0.75f); // Steel
            battleHelm.allowColorCustomization = false;
            battleHelm.metallic = 0.85f;
            battleHelm.roughness = 0.3f;
            accessories[battleHelm.id] = battleHelm;
        }

        {
            CatAccessory witchHat;
            witchHat.id = "hat_witch";
            witchHat.name = "Witch Hat";
            witchHat.description = "Perfect for Halloween mischief";
            witchHat.slot = AccessorySlot::Head;
            witchHat.modelPath = "assets/models/accessories/witch_hat.glb";
            witchHat.positionOffset = glm::vec3(0.0f, 0.08f, 0.02f);
            witchHat.rotationOffset = glm::vec3(5.0f, 0.0f, 0.0f);
            witchHat.scale = 1.0f;
            witchHat.isUnlocked = false;
            witchHat.unlockCondition = "event_halloween";
            witchHat.unlockLevel = 1;
            witchHat.tintColor = glm::vec3(0.1f, 0.1f, 0.1f); // Black
            witchHat.allowColorCustomization = true;
            witchHat.metallic = 0.0f;
            witchHat.roughness = 0.6f;
            accessories[witchHat.id] = witchHat;
        }

        {
            CatAccessory partyHat;
            partyHat.id = "hat_party";
            partyHat.name = "Party Hat";
            partyHat.description = "Let's celebrate!";
            partyHat.slot = AccessorySlot::Head;
            partyHat.modelPath = "assets/models/accessories/party_hat.glb";
            partyHat.positionOffset = glm::vec3(0.0f, 0.06f, 0.0f);
            partyHat.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            partyHat.scale = 0.9f;
            partyHat.isUnlocked = false;
            partyHat.unlockCondition = "event_birthday";
            partyHat.unlockLevel = 1;
            partyHat.tintColor = glm::vec3(0.9f, 0.2f, 0.2f); // Red
            partyHat.allowColorCustomization = true;
            partyHat.metallic = 0.0f;
            partyHat.roughness = 0.7f;
            accessories[partyHat.id] = partyHat;
        }

        {
            CatAccessory halo;
            halo.id = "halo_angel";
            halo.name = "Angel Halo";
            halo.description = "For the most virtuous of cats";
            halo.slot = AccessorySlot::Head;
            halo.modelPath = "assets/models/accessories/halo.glb";
            halo.positionOffset = glm::vec3(0.0f, 0.15f, 0.0f);
            halo.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            halo.scale = 0.7f;
            halo.isUnlocked = false;
            halo.unlockCondition = "achievement_pacifist";
            halo.unlockLevel = 10;
            halo.tintColor = glm::vec3(1.0f, 1.0f, 0.9f); // Golden white
            halo.allowColorCustomization = false;
            halo.metallic = 0.5f;
            halo.roughness = 0.1f;
            halo.particleEffect = "holy_glow";
            accessories[halo.id] = halo;
        }

        // ====================================================================
        // NECK ACCESSORIES
        // ====================================================================

        {
            CatAccessory collarBell;
            collarBell.id = "collar_bell";
            collarBell.name = "Collar with Bell";
            collarBell.description = "A classic cat collar with a jingling bell";
            collarBell.slot = AccessorySlot::Neck;
            collarBell.modelPath = "assets/models/accessories/collar_bell.glb";
            collarBell.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            collarBell.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            collarBell.scale = 1.0f;
            collarBell.isUnlocked = true; // Start unlocked
            collarBell.unlockCondition = "default";
            collarBell.unlockLevel = 0;
            collarBell.tintColor = glm::vec3(0.8f, 0.2f, 0.2f); // Red
            collarBell.allowColorCustomization = true;
            collarBell.metallic = 0.0f;
            collarBell.roughness = 0.5f;
            accessories[collarBell.id] = collarBell;
        }

        {
            CatAccessory clanMist;
            clanMist.id = "collar_mistclan";
            clanMist.name = "MistClan Collar";
            clanMist.description = "Symbol of MistClan allegiance";
            clanMist.slot = AccessorySlot::Neck;
            clanMist.modelPath = "assets/models/accessories/clan_collar.glb";
            clanMist.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            clanMist.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            clanMist.scale = 1.0f;
            clanMist.isUnlocked = false;
            clanMist.unlockCondition = "clan_mistclan";
            clanMist.unlockLevel = 5;
            clanMist.tintColor = glm::vec3(0.7f, 0.8f, 0.9f); // Misty blue
            clanMist.allowColorCustomization = false;
            clanMist.metallic = 0.2f;
            clanMist.roughness = 0.4f;
            accessories[clanMist.id] = clanMist;
        }

        {
            CatAccessory clanStorm;
            clanStorm.id = "collar_stormclan";
            clanStorm.name = "StormClan Collar";
            clanStorm.description = "Symbol of StormClan allegiance";
            clanStorm.slot = AccessorySlot::Neck;
            clanStorm.modelPath = "assets/models/accessories/clan_collar.glb";
            clanStorm.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            clanStorm.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            clanStorm.scale = 1.0f;
            clanStorm.isUnlocked = false;
            clanStorm.unlockCondition = "clan_stormclan";
            clanStorm.unlockLevel = 5;
            clanStorm.tintColor = glm::vec3(0.3f, 0.3f, 0.4f); // Storm grey
            clanStorm.allowColorCustomization = false;
            clanStorm.metallic = 0.2f;
            clanStorm.roughness = 0.4f;
            clanStorm.particleEffect = "lightning_spark";
            accessories[clanStorm.id] = clanStorm;
        }

        {
            CatAccessory clanEmber;
            clanEmber.id = "collar_emberclan";
            clanEmber.name = "EmberClan Collar";
            clanEmber.description = "Symbol of EmberClan allegiance";
            clanEmber.slot = AccessorySlot::Neck;
            clanEmber.modelPath = "assets/models/accessories/clan_collar.glb";
            clanEmber.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            clanEmber.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            clanEmber.scale = 1.0f;
            clanEmber.isUnlocked = false;
            clanEmber.unlockCondition = "clan_emberclan";
            clanEmber.unlockLevel = 5;
            clanEmber.tintColor = glm::vec3(1.0f, 0.4f, 0.1f); // Fiery orange
            clanEmber.allowColorCustomization = false;
            clanEmber.metallic = 0.2f;
            clanEmber.roughness = 0.4f;
            clanEmber.particleEffect = "ember_glow";
            accessories[clanEmber.id] = clanEmber;
        }

        {
            CatAccessory clanFrost;
            clanFrost.id = "collar_frostclan";
            clanFrost.name = "FrostClan Collar";
            clanFrost.description = "Symbol of FrostClan allegiance";
            clanFrost.slot = AccessorySlot::Neck;
            clanFrost.modelPath = "assets/models/accessories/clan_collar.glb";
            clanFrost.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            clanFrost.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            clanFrost.scale = 1.0f;
            clanFrost.isUnlocked = false;
            clanFrost.unlockCondition = "clan_frostclan";
            clanFrost.unlockLevel = 5;
            clanFrost.tintColor = glm::vec3(0.8f, 0.9f, 1.0f); // Icy blue
            clanFrost.allowColorCustomization = false;
            clanFrost.metallic = 0.2f;
            clanFrost.roughness = 0.4f;
            clanFrost.particleEffect = "frost_sparkle";
            accessories[clanFrost.id] = clanFrost;
        }

        {
            CatAccessory bandana;
            bandana.id = "bandana_red";
            bandana.name = "Bandana";
            bandana.description = "A stylish bandana";
            bandana.slot = AccessorySlot::Neck;
            bandana.modelPath = "assets/models/accessories/bandana.glb";
            bandana.positionOffset = glm::vec3(0.0f, -0.05f, 0.05f);
            bandana.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            bandana.scale = 1.0f;
            bandana.isUnlocked = false;
            bandana.unlockCondition = "level_3";
            bandana.unlockLevel = 3;
            bandana.tintColor = glm::vec3(0.9f, 0.2f, 0.2f); // Red
            bandana.allowColorCustomization = true;
            bandana.metallic = 0.0f;
            bandana.roughness = 0.8f;
            accessories[bandana.id] = bandana;
        }

        {
            CatAccessory bowTie;
            bowTie.id = "bowtie_formal";
            bowTie.name = "Bow Tie";
            bowTie.description = "For the distinguished feline";
            bowTie.slot = AccessorySlot::Neck;
            bowTie.modelPath = "assets/models/accessories/bowtie.glb";
            bowTie.positionOffset = glm::vec3(0.0f, -0.02f, 0.08f);
            bowTie.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            bowTie.scale = 0.8f;
            bowTie.isUnlocked = false;
            bowTie.unlockCondition = "achievement_gentleman";
            bowTie.unlockLevel = 8;
            bowTie.tintColor = glm::vec3(0.1f, 0.1f, 0.1f); // Black
            bowTie.allowColorCustomization = true;
            bowTie.metallic = 0.1f;
            bowTie.roughness = 0.6f;
            accessories[bowTie.id] = bowTie;
        }

        {
            CatAccessory medallion;
            medallion.id = "medallion_hero";
            medallion.name = "Hero's Medallion";
            medallion.description = "Awarded for exceptional bravery";
            medallion.slot = AccessorySlot::Neck;
            medallion.modelPath = "assets/models/accessories/medallion.glb";
            medallion.positionOffset = glm::vec3(0.0f, -0.08f, 0.08f);
            medallion.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            medallion.scale = 0.6f;
            medallion.isUnlocked = false;
            medallion.unlockCondition = "quest_hero_journey";
            medallion.unlockLevel = 25;
            medallion.tintColor = glm::vec3(1.0f, 0.84f, 0.0f); // Gold
            medallion.allowColorCustomization = false;
            medallion.metallic = 0.95f;
            medallion.roughness = 0.15f;
            medallion.particleEffect = "golden_shimmer";
            accessories[medallion.id] = medallion;
        }

        // ====================================================================
        // BACK ACCESSORIES
        // ====================================================================

        {
            CatAccessory capeRed;
            capeRed.id = "cape_red";
            capeRed.name = "Red Cape";
            capeRed.description = "A heroic cape that billows in the wind";
            capeRed.slot = AccessorySlot::Back;
            capeRed.modelPath = "assets/models/accessories/cape.glb";
            capeRed.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            capeRed.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            capeRed.scale = 1.0f;
            capeRed.isUnlocked = false;
            capeRed.unlockCondition = "level_7";
            capeRed.unlockLevel = 7;
            capeRed.tintColor = glm::vec3(0.8f, 0.1f, 0.1f); // Red
            capeRed.allowColorCustomization = true;
            capeRed.metallic = 0.0f;
            capeRed.roughness = 0.7f;
            accessories[capeRed.id] = capeRed;
        }

        {
            CatAccessory wings;
            wings.id = "wings_angel";
            wings.name = "Angel Wings";
            wings.description = "Cosmetic wings (you can't actually fly... or can you?)";
            wings.slot = AccessorySlot::Back;
            wings.modelPath = "assets/models/accessories/wings.glb";
            wings.positionOffset = glm::vec3(0.0f, 0.05f, -0.05f);
            wings.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            wings.scale = 1.2f;
            wings.isUnlocked = false;
            wings.unlockCondition = "achievement_skybound";
            wings.unlockLevel = 15;
            wings.tintColor = glm::vec3(1.0f, 1.0f, 1.0f); // White
            wings.allowColorCustomization = true;
            wings.metallic = 0.0f;
            wings.roughness = 0.4f;
            wings.particleEffect = "feather_sparkle";
            accessories[wings.id] = wings;
        }

        {
            CatAccessory saddle;
            saddle.id = "saddle_mount";
            saddle.name = "Riding Saddle";
            saddle.description = "For other smaller cats to ride on?";
            saddle.slot = AccessorySlot::Back;
            saddle.modelPath = "assets/models/accessories/saddle.glb";
            saddle.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            saddle.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            saddle.scale = 0.9f;
            saddle.isUnlocked = false;
            saddle.unlockCondition = "achievement_mount_master";
            saddle.unlockLevel = 12;
            saddle.tintColor = glm::vec3(0.5f, 0.3f, 0.2f); // Brown leather
            saddle.allowColorCustomization = true;
            saddle.metallic = 0.0f;
            saddle.roughness = 0.7f;
            accessories[saddle.id] = saddle;
        }

        {
            CatAccessory backpack;
            backpack.id = "backpack_adventurer";
            backpack.name = "Adventurer's Backpack";
            backpack.description = "Carry all your treasures";
            backpack.slot = AccessorySlot::Back;
            backpack.modelPath = "assets/models/accessories/backpack.glb";
            backpack.positionOffset = glm::vec3(0.0f, 0.0f, -0.05f);
            backpack.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            backpack.scale = 0.7f;
            backpack.isUnlocked = false;
            backpack.unlockCondition = "quest_explorer";
            backpack.unlockLevel = 5;
            backpack.tintColor = glm::vec3(0.4f, 0.5f, 0.3f); // Forest green
            backpack.allowColorCustomization = true;
            backpack.metallic = 0.0f;
            backpack.roughness = 0.8f;
            accessories[backpack.id] = backpack;
        }

        {
            CatAccessory jetpack;
            jetpack.id = "jetpack_future";
            jetpack.name = "Jetpack";
            jetpack.description = "High-tech propulsion system";
            jetpack.slot = AccessorySlot::Back;
            jetpack.modelPath = "assets/models/accessories/jetpack.glb";
            jetpack.positionOffset = glm::vec3(0.0f, 0.02f, -0.08f);
            jetpack.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            jetpack.scale = 0.8f;
            jetpack.isUnlocked = false;
            jetpack.unlockCondition = "achievement_tech_master";
            jetpack.unlockLevel = 30;
            jetpack.tintColor = glm::vec3(0.7f, 0.7f, 0.8f); // Metallic grey
            jetpack.allowColorCustomization = true;
            jetpack.metallic = 0.9f;
            jetpack.roughness = 0.2f;
            jetpack.particleEffect = "rocket_thrust";
            accessories[jetpack.id] = jetpack;
        }

        // ====================================================================
        // TAIL ACCESSORIES
        // ====================================================================

        {
            CatAccessory ribbonPink;
            ribbonPink.id = "ribbon_pink";
            ribbonPink.name = "Pink Ribbon";
            ribbonPink.description = "A cute ribbon tied around your tail";
            ribbonPink.slot = AccessorySlot::Tail;
            ribbonPink.modelPath = "assets/models/accessories/ribbon.glb";
            ribbonPink.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            ribbonPink.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            ribbonPink.scale = 1.0f;
            ribbonPink.isUnlocked = true; // Start unlocked
            ribbonPink.unlockCondition = "default";
            ribbonPink.unlockLevel = 0;
            ribbonPink.tintColor = glm::vec3(1.0f, 0.7f, 0.8f); // Pink
            ribbonPink.allowColorCustomization = true;
            ribbonPink.metallic = 0.0f;
            ribbonPink.roughness = 0.6f;
            accessories[ribbonPink.id] = ribbonPink;
        }

        {
            CatAccessory tailRing;
            tailRing.id = "ring_tail";
            tailRing.name = "Tail Ring";
            tailRing.description = "An ornate ring for your tail";
            tailRing.slot = AccessorySlot::Tail;
            tailRing.modelPath = "assets/models/accessories/tail_ring.glb";
            tailRing.positionOffset = glm::vec3(0.0f, 0.0f, 0.1f);
            tailRing.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            tailRing.scale = 0.9f;
            tailRing.isUnlocked = false;
            tailRing.unlockCondition = "level_5";
            tailRing.unlockLevel = 5;
            tailRing.tintColor = glm::vec3(0.8f, 0.8f, 0.85f); // Silver
            tailRing.allowColorCustomization = true;
            tailRing.metallic = 0.9f;
            tailRing.roughness = 0.2f;
            accessories[tailRing.id] = tailRing;
        }

        {
            CatAccessory flameEffect;
            flameEffect.id = "effect_flame";
            flameEffect.name = "Flame Aura";
            flameEffect.description = "Your tail burns with elemental fire";
            flameEffect.slot = AccessorySlot::Tail;
            flameEffect.modelPath = "assets/models/accessories/tail_effect.glb";
            flameEffect.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            flameEffect.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            flameEffect.scale = 1.1f;
            flameEffect.isUnlocked = false;
            flameEffect.unlockCondition = "mastery_fire";
            flameEffect.unlockLevel = 20;
            flameEffect.tintColor = glm::vec3(1.0f, 0.5f, 0.1f); // Fire orange
            flameEffect.allowColorCustomization = false;
            flameEffect.metallic = 0.0f;
            flameEffect.roughness = 1.0f;
            flameEffect.particleEffect = "fire_trail";
            accessories[flameEffect.id] = flameEffect;
        }

        {
            CatAccessory iceEffect;
            iceEffect.id = "effect_ice";
            iceEffect.name = "Frost Crystals";
            iceEffect.description = "Ice crystals form along your tail";
            iceEffect.slot = AccessorySlot::Tail;
            iceEffect.modelPath = "assets/models/accessories/tail_effect.glb";
            iceEffect.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            iceEffect.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            iceEffect.scale = 1.1f;
            iceEffect.isUnlocked = false;
            iceEffect.unlockCondition = "mastery_ice";
            iceEffect.unlockLevel = 20;
            iceEffect.tintColor = glm::vec3(0.7f, 0.9f, 1.0f); // Icy blue
            iceEffect.allowColorCustomization = false;
            iceEffect.metallic = 0.3f;
            iceEffect.roughness = 0.1f;
            iceEffect.particleEffect = "ice_trail";
            accessories[iceEffect.id] = iceEffect;
        }

        {
            CatAccessory lightningEffect;
            lightningEffect.id = "effect_lightning";
            lightningEffect.name = "Lightning Spark";
            lightningEffect.description = "Electricity crackles around your tail";
            lightningEffect.slot = AccessorySlot::Tail;
            lightningEffect.modelPath = "assets/models/accessories/tail_effect.glb";
            lightningEffect.positionOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            lightningEffect.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            lightningEffect.scale = 1.1f;
            lightningEffect.isUnlocked = false;
            lightningEffect.unlockCondition = "mastery_lightning";
            lightningEffect.unlockLevel = 20;
            lightningEffect.tintColor = glm::vec3(0.8f, 0.9f, 1.0f); // Electric blue
            lightningEffect.allowColorCustomization = false;
            lightningEffect.metallic = 0.0f;
            lightningEffect.roughness = 1.0f;
            lightningEffect.particleEffect = "lightning_trail";
            accessories[lightningEffect.id] = lightningEffect;
        }

        {
            CatAccessory bellTail;
            bellTail.id = "bell_tail";
            bellTail.name = "Tail Bell";
            bellTail.description = "A jingling bell at the end of your tail";
            bellTail.slot = AccessorySlot::Tail;
            bellTail.modelPath = "assets/models/accessories/bell.glb";
            bellTail.positionOffset = glm::vec3(0.0f, 0.0f, -0.3f);
            bellTail.rotationOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            bellTail.scale = 0.5f;
            bellTail.isUnlocked = false;
            bellTail.unlockCondition = "level_2";
            bellTail.unlockLevel = 2;
            bellTail.tintColor = glm::vec3(1.0f, 0.84f, 0.0f); // Gold
            bellTail.allowColorCustomization = true;
            bellTail.metallic = 0.8f;
            bellTail.roughness = 0.3f;
            accessories[bellTail.id] = bellTail;
        }

        return accessories;
    }
};

} // namespace CatGame
