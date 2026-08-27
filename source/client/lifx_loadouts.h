#pragma once
/* ===================================================================================
	A2a #125 / 2b route B — per-NPC-type equipment LOADOUTS.

	The server classifies each player-model NPC and sends a small loadout ID over
	the ghost (marker + u8 id). The client owns the ID -> mesh-set tables here, so
	the wire stays tiny and the server never needs to know the client's mesh-array
	ordering.

	Each loadout's mesh list is DERIVED FROM REAL EQUIP ITEMS: the names come
	straight from data/cm_equipTypes.xml <mesh> entries (the same item->mesh table
	the engine uses to render a player's worn gear). So this is "real item -> real
	mesh," just sourced from a per-type loadout definition instead of a live
	inventory (the live-inventory path is the later Strategy-P bind work).

	ApplyEquip hides all armor + hair/beard/underwear, then SHOWS the loadout's
	meshes by name. Add a new archetype by adding a row here + a server mapping.
*  =================================================================================== */

struct LifxLoadout {
	const char*        name;
	const char* const* meshes;   // gear to SHOW
	unsigned           count;
	bool               bareBody;  // swap the default CLOTHED body for bare skin
};

// male.dts has TWO body representations: "Male_Body_ALL" is the default body
// that includes a CLOTHED chest/tunic (what the engine shows when you have legs
// but no chest armor), and the individual "Male_Body_v1_*" parts are the BARE
// skin the game swaps to once you wear armor. A bare NPC keeps Male_Body_ALL, so
// its built-in chest tunic shows under a thin chest piece (leather) as the "still
// there" cloth. bareBody=true hides Male_Body_ALL and shows the bare parts, so
// the armor sits over skin. Plate's bulk fully hides Male_Body_ALL already and
// renders clean, so it leaves the default body alone.
static const char* const kDefaultBodyHide[] = { "Male_Body_ALL" };
static const char* const kBareBodyShow[] = {
	"Male_Body_v1_arm", "Male_Body_v1_feet", "Male_Body_v1_forearms",
	"Male_Body_v1_shin", "Male_Body_v1_shoulders",
	"Male_Body_v1_torso", "Male_Body_v1_torso_B",
	"Male_Body_v1_torso_block", "Male_Body_v1_torso_C",
};
static const unsigned kDefaultBodyHideCount = sizeof(kDefaultBodyHide) / sizeof(kDefaultBodyHide[0]);
static const unsigned kBareBodyShowCount     = sizeof(kBareBodyShow)     / sizeof(kBareBodyShow[0]);

// id 0 — Full Plate (royal full plate set; cm_equipTypes <mesh> names)
static const char* const kLoadoutPlate[] = {
	"Full_Plate_Helmet", "Full_Plate_Body", "Full_Plate_Arm",
	"Full_Plate_Forearm", "Full_Plate_Legs", "Full_Plate_Feet",
};

// id 1 — Novice Leather (cm_equipTypes <mesh> names)
static const char* const kLoadoutLeather[] = {
	"Novice_Leather_Helmet", "Novice_Leather_Body", "Novice_Leather_Arm",
	"Novice_Leather_Forearm", "Novice_Leather_Legs", "Novice_Leather_Feet",
};

static const LifxLoadout kLifxLoadouts[] = {
	// plate: leave the default body (its bulk covers it, renders clean).
	// leather: thin -> swap to bare skin so the default chest tunic doesn't show.
	{ "plate",   kLoadoutPlate,   sizeof(kLoadoutPlate)   / sizeof(kLoadoutPlate[0]),   false },
	{ "leather", kLoadoutLeather, sizeof(kLoadoutLeather) / sizeof(kLoadoutLeather[0]), true  },
};
static const unsigned kLifxLoadoutCount = sizeof(kLifxLoadouts) / sizeof(kLifxLoadouts[0]);
