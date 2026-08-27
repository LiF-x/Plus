//-----------------------------------------------------------------------------
// LiFx Battlezones — boot re-seeder, snap-back notifier, minimal arena controller.
//
// Deploy: copy this folder to  <server>/mods/Battlezones/  and ensure your
// mod loader execs mods/Battlezones/battlezones.cs (the same way
// mods/AutoloadConfig.cs is loaded). Requires the LiFx DLL build that exposes
// the Lifx::*BattleZone* console commands (see docs/battlezones.md).
//
// Battlezones are NOT persisted by the engine — they vanish on restart. This
// script recreates the configured set every boot, which is the persistence
// story for "permanent" zones.
//-----------------------------------------------------------------------------

// ----- Config ----------------------------------------------------------------
// Define zones to (re)create at boot. type==1 + active==1 confines players;
// any other type is a passive, raid-protected, tracked region.
//
//   $LiFx::battlezone::count
//   $LiFx::battlezone[i, "geoId"]  $LiFx::battlezone[i,"radius"]
//   $LiFx::battlezone[i, "name"]   $LiFx::battlezone[i,"type"]
//   $LiFx::battlezone[i, "active"]
//
// Example (commented out — set real geoIds for your map):
// $LiFx::battlezone::count = 1;
// $LiFx::battlezone[0, "geoId"]  = 123456;
// $LiFx::battlezone[0, "radius"] = 20;
// $LiFx::battlezone[0, "name"]   = "DuelRing";
// $LiFx::battlezone[0, "type"]   = 1;
// $LiFx::battlezone[0, "active"] = 1;

if (!isObject($LiFx::battlezone::count))
   $LiFx::battlezone::count = $LiFx::battlezone::count; // no-op guard for re-exec

// ----- Re-seeder -------------------------------------------------------------
function LifxBattleZoneSeedAll()
{
   %count = $LiFx::battlezone::count;
   if (%count $= "" || %count <= 0) {
      echo("[lifx-battlezone] no zones configured; nothing to seed");
      return;
   }
   for (%i = 0; %i < %count; %i++) {
      %geo    = $LiFx::battlezone[%i, "geoId"];
      %radius = $LiFx::battlezone[%i, "radius"];
      %name   = $LiFx::battlezone[%i, "name"];
      %type   = $LiFx::battlezone[%i, "type"];
      %active = $LiFx::battlezone[%i, "active"];
      if (%type $= "") %type = 1;
      if (%radius $= "" || %radius <= 0) %radius = 20;

      %landId = Lifx::createBattleZone(%type, %geo, %radius, %name);
      if (%landId >= 0 && %active !$= "" && %active != 0)
         Lifx::setBattleZoneActive(%landId, 1);
      echo("[lifx-battlezone] seeded '" @ %name @ "' geo=" @ %geo @ " -> landId=" @ %landId);
   }
}

// Run the seeder once the world/lands are up. onServerCreated is the standard
// Torque post-world-init callback; if your build fires lands init later, move
// this call to a later hook (see docs/battlezones.md note on initialLoadGuildLandsFromDB).
package LifxBattlezones {
   function onServerCreated()
   {
      Parent::onServerCreated();
      LifxBattleZoneSeedAll();
   }
};
activatePackage(LifxBattlezones);

// ----- Snap-back notifier ----------------------------------------------------
// Called from the LiFx containment hook (throttled) when a NON-exempt player is
// held inside an armed battlezone. Show them a notice. The engine string
// id 4761 is "Return to fight!" (present in cm_messages.xml).
//
// NOTE: mapping a charId to a live client + sending a message is server-build
// specific. Wire the body to your server's existing helper. The block below is
// a best-effort using common LiF/Torque patterns — adapt as needed.
function LifxBattleZoneOnContained(%charId)
{
   %client = LifxFindClientByCharId(%charId);
   if (isObject(%client))
      commandToClient(%client, 'cmShowMessageById', 4761); // "Return to fight!"
}

// Best-effort charId -> GameConnection lookup. Replace with your server's
// canonical resolver if it has one.
function LifxFindClientByCharId(%charId)
{
   %count = ClientGroup.getCount();
   for (%i = 0; %i < %count; %i++) {
      %cl = ClientGroup.getObject(%i);
      if (isObject(%cl) && %cl.charId == %charId)
         return %cl;
   }
   return 0;
}

// ----- Minimal arena controller ---------------------------------------------
// Stand up an armed zone, (optionally) corral a list of players in, hold for a
// duration, then tear it down. Capture/scoring/enlist UI are out of scope.
//
//   %playerList : space-separated charIds to teleport in (may be empty)
function lifxArenaStart(%geoId, %radius, %name, %durationSecs, %playerList)
{
   %landId = Lifx::createBattleZone(1, %geoId, %radius, %name);
   if (%landId < 0) {
      warn("[lifx-arena] failed to create zone '" @ %name @ "'");
      return -1;
   }
   Lifx::setBattleZoneActive(%landId, 1);

   // Teleport enlisted players to the zone center (server-specific helper).
   %n = getWordCount(%playerList);
   for (%i = 0; %i < %n; %i++) {
      %cid = getWord(%playerList, %i);
      LifxTeleportCharToGeo(%cid, %geoId); // implement against your teleport API
   }

   if (%durationSecs > 0)
      schedule(%durationSecs * 1000, 0, "lifxArenaEnd", %landId, %geoId);
   echo("[lifx-arena] started '" @ %name @ "' landId=" @ %landId @ " for " @ %durationSecs @ "s");
   return %landId;
}

function lifxArenaEnd(%landId, %exitGeoId)
{
   // Disarm first so the snap-back stops, then remove the zone.
   Lifx::setBattleZoneActive(%landId, 0);
   Lifx::deleteBattleZone(%landId);
   echo("[lifx-arena] ended landId=" @ %landId);
   // (Optional) teleport survivors to %exitGeoId via your teleport helper.
}

// Stub — wire to your server's "move character to geoId" routine.
function LifxTeleportCharToGeo(%charId, %geoId)
{
   // intentionally empty: integration point
}

echo("[lifx-battlezone] battlezones.cs loaded");
