//-----------------------------------------------------------------------------
// HostileNPCs mod - Stage 0
// A hostile humanoid NPC that uses the PLAYER male body model on the proven,
// fully data-driven AnimalData combat path.
//
// IMPORTANT (learned from a server crash): loading an AnimalData that inherits
// DefaultPlayerData directly - with a custom animalTypeId and explicit datablock
// id - crashes the server during datablock preload at onServerCreated. The robust
// approach is to inherit the already-loaded, known-good WolfData and override ONLY
// what makes this a humanoid hostile. That way the animalTypeId (755), weapon,
// hit-trace fields, corpse ids, speeds and body radius are all valid and inherited.
//
// Trade-off for Stage 0: the entity is internally typed as the "Wolf" animal type
// (755) but renders the male model. A dedicated "Bandit" object type can be added
// later once the base load is confirmed stable.
//-----------------------------------------------------------------------------

datablock AnimalData(BanditData : WolfData)
{
   // Explicit, fixed datablock id so the CLIENT (which loads this same file via
   // cmod.cs) and the SERVER (via mod.cs) agree on the id when the entity is
   // ghosted - otherwise the client can't map/render the bandit. 250 sits in a
   // large free gap (180-273) in the datablock id space. NOTE: the first attempt
   // used id 147, which collides with a weapon (weapons/LiFWeapons.cs) - that id
   // collision is what crashed the server on datablock load.
   id = 250;

   // The one change that makes this a humanoid:
   shapeFile = "art/models/3d/mobiles/characters/male.dts";

   // Aggressive AI (Wolf tree, DeathHandler intact, engages quickly).
   behavior = "data/ai/aiBanditAggressive.xml";

   maxHP = 120.0;

   // Everything else (animalTypeId 755, weaponData, powerHit*/fastHit*,
   // rawCorpseObjectTypeID/skinnedCorpseObjectTypeID, speeds, bodyRadius) is
   // inherited from WolfData.
};
