/**
* <name>HostileNPCs (client)</name>
* <description>Client-side companion to the HostileNPCs server mod. Registers the
*   BanditData datablock on the client so the player-model hostile renders.</description>
* <license>GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007</license>
*/

if (!isObject(HostileNPCs))
{
    new ScriptObject(HostileNPCs) {};
}

package HostileNPCs
{
  function HostileNPCs::setup()
  {
    // onDatablockLoad fires when base datablocks (incl. DefaultPlayerData) are
    // available - the correct moment to register a datablock that inherits it.
    LiFx::registerCallback($LiFx::hooks::onDatablockLoad, RegisterDatablock, HostileNPCs);
  }

  function HostileNPCs::RegisterDatablock()
  {
    if (!isObject(BanditData))
      LiFx::loadRecursivelyInFolder("yolauncher/modpack/mods/HostileNPCs/art/datablocks", "Bandit.cs");
  }
};

activatePackage(HostileNPCs);
LiFx::registerCallback($LiFx::hooks::mods, setup, HostileNPCs);
