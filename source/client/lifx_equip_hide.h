#pragma once
/* ===================================================================================
	A2a #125 — extra mesh hides for EQUIPPED player-model NPCs.

	A bare NPCDecorative has no character-customization data, so the male.dts
	shape defaults EVERY hair and beard variant to visible at once (they normally
	get culled to the one chosen style by appearance data we don't have). When the
	NPC wears a closed helm this reads as "all the hair types poking through."

	These are the hair/beard MESH object names from male.dts (the _DIFFUSE entries
	are materials, excluded). setMeshHidden() on a name the shape doesn't have is a
	harmless no-op, so over-listing is safe. Hidden by ApplyEquip in addition to
	the armor/clothing set.

	NOTE (2b): when a loadout has NO helmet we'll want to KEEP one hair style; for
	the current full-plate set (helmeted) hiding all hair/beard is correct.
*  =================================================================================== */

static const char* kLifxEquipExtraHide[] = {
	// --- beards ---
	"Male_Beard_All_v1_cut", "Male_Beard_All_v1_up",
	"Male_Beard_All_v2_cut", "Male_Beard_All_v2_up",
	"Male_Beard_All_v3_cut", "Male_Beard_All_v3_up",
	"Male_Beard_All_v4_cut", "Male_Beard_All_v4_up",
	"Male_Beard_Eur_v1_cut", "Male_Beard_Eur_v1_up",
	"Male_Beard_Eur_v2_cut", "Male_Beard_Eur_v2_up",
	"Male_Beard_Eur_v3_cut", "Male_Beard_Eur_v3_up",
	"Male_Beard_Eur_v4_cut", "Male_Beard_Eur_v4_up",
	"Male_Beard_Mon_v1_cut", "Male_Beard_Mon_v1_up",
	"Male_Beard_Mon_v2_cut", "Male_Beard_Mon_v2_up",
	"Male_Beard_Mon_v3_cut", "Male_Beard_Mon_v3_up",
	"Male_Beard_Mon_v4_cut", "Male_Beard_Mon_v4_up",
	"Male_Beard_Vik_v1_cut", "Male_Beard_Vik_v1_up",
	"Male_Beard_Vik_v2_up",
	"Male_Beard_Vik_v3_up",
	"Male_Beard_Vik_v4_cut", "Male_Beard_Vik_v4_up",
	// --- hair ---
	"Male_Hair_All_v1_cut", "Male_Hair_All_v1_up",
	"Male_Hair_All_v2_cut", "Male_Hair_All_v2_up",
	"Male_Hair_All_v3_up",
	"Male_Hair_All_v4_cut", "Male_Hair_All_v4_up",
	"Male_Hair_Eur_v1_cut", "Male_Hair_Eur_v1_up",
	"Male_Hair_Eur_v2_cut", "Male_Hair_Eur_v2_up",
	"Male_Hair_Eur_v3_up",
	"Male_Hair_Eur_v4_up",
	"Male_Hair_Mon_v1_up",
	"Male_Hair_Mon_v2_cut", "Male_Hair_Mon_v2_up",
	"Male_Hair_Mon_v3_up",
	"Male_Hair_Mon_v4_cut", "Male_Hair_Mon_v4_up",
	"Male_Hair_Vik_v1_up",
	"Male_Hair_Vik_v2_cut", "Male_Hair_Vik_v2_up",
	"Male_Hair_Vik_v3_up",
	"Male_Hair_Vik_v4_up",
	// --- underwear (all variants render at once with no appearance data → a
	//     layered "skirt" under the plate; the cuirass/greaves cover this) ---
	"Male_Underwear_Eur_Hips", "Male_Underwear_Eur_Hips_B",
	"Male_Underwear_Mon_Body", "Male_Underwear_Mon_Feet",
	"Male_Underwear_Mon_Hips", "Male_Underwear_Mon_Hips_B",
	"Male_Underwear_Vik_Arm",  "Male_Underwear_Vik_Body",
	"Male_Underwear_Vik_Feet", "Male_Underwear_Vik_Hips", "Male_Underwear_Vik_Hips_B",
	// Seam-filler strip at body joints (incl. the waist/torso seam). Plate hides
	// it; under thin leather it can show as a waist band. The only un-categorised
	// visible mesh left, so the prime suspect for the "default chest"/skirt.
	"Male_Antiseam",
};
static const unsigned kLifxEquipExtraHideCount =
	sizeof(kLifxEquipExtraHide) / sizeof(kLifxEquipExtraHide[0]);
