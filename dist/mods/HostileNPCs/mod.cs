/**
* <name>HostileNPCs</name>
* <description>Player-model hostile humanoid NPC for LiF:YO (LiFx). Stage 0:
*   a humanoid that uses the player male body model, fights players with a real
*   hit-trace weapon, and leaves a skinnable corpse on death. Built on the
*   data-driven AnimalData combat path.</description>
* <license>GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007</license>
*/

// LiFx expects each mod to register itself as a ScriptObject and as its own package.
if (!isObject(HostileNPCs))
{
    new ScriptObject(HostileNPCs)
    {
    };
}

package HostileNPCs
{
  function HostileNPCs::version() {
    return "v0.1.0";
  }

  function HostileNPCs::setup() {
    HostileNPCs.modRoot = getSubStr($Con::File, 0, strrchrpos($Con::File, "/") + 1);

    // Load the Bandit datablock AFTER the server (and its base datablocks,
    // including WolfData) are created. preServerCreated fires too early; the
    // onServerCreated hook is unambiguously after base datablocks are registered.
    // (BanditData inherits WolfData, which is loaded by this point.)
    LiFx::registerCallback($LiFx::hooks::onServerCreatedCallbacks, loadDatablocks, HostileNPCs);
  }

  function HostileNPCs::loadDatablocks() {
    // Guard against double-definition (the same file may be reachable from more
    // than one load path). NOTE: server-side uses the BARE loadRecursivelyInFolder
    // (the LiFx:: prefixed variant only exists client-side - see cmod.cs). The
    // behaviour XML is deployed to data/ai/ (see deploy.sh) so the engine resolves
    // BanditData.behavior like the wolf's.
    if (!isObject(BanditData))
      loadRecursivelyInFolder("yolauncher/modpack/mods/HostileNPCs/art/datablocks", "Bandit.cs");

    // A2a (#125): a player-model NPCData so worn-armor sub-meshes exist to reveal.
    // An NPCData (like the slaves) passes NPCDecorative::onAdd; a raw PlayerData
    // does NOT. This def is byte-identical (id + fields) to the CLIENT-side def in
    // the client's art/datablocks/npc/npc.cs so the ghosted datablock id (251)
    // resolves on the client (which runs raw, no modpack) instead of crashing it.
    if (!isObject(NPC_player_male))
      datablock NPCData(NPC_player_male : DefaultPlayerData) {
        id = 251;
        shapeFile = "art/models/3d/mobiles/characters/male.dts";
        behavior = "data/ai/lifxWanderTest.xml";
        maxHP = 100.0;
        hpRegenRate = 1.0;
      };
  }

  // ---- GM test spawn -----------------------------------------------------------
  // Convenience command to drop a Bandit at the caller for quick testing.
  // NOTE: `spawnObject` is the spawn primitive documented in
  // docs/ai_and_spawning.md; if this build does not expose it, use the
  // wild spawn pattern instead (deploy.sh adds a BanditData family to
  // data/cm_spawn_patterns.xml). Verify at runtime and adjust if needed.
  function serverCmdSpawnBandit(%client) {
    %player = %client.player;
    if (!isObject(%player)) {
      LiFx::debugEcho("[HostileNPCs] SpawnBandit: caller has no player object");
      return;
    }
    // Type is the inherited Wolf animal type (755); datablock is BanditData.
    %bandit = spawnObject("Wolf", "BanditData");
    if (isObject(%bandit)) {
      %bandit.setTransform(%player.getTransform());
      LiFx::debugEcho("[HostileNPCs] spawned Bandit " @ %bandit @ " at caller");
    } else {
      LiFx::debugEcho("[HostileNPCs] SpawnBandit failed - spawnObject unavailable; use the wild spawn pattern");
    }
  }
};
activatePackage(HostileNPCs);
