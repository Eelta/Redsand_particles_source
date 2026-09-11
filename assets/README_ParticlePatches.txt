PrecisionParticleMatch optional particle scope patches
Author: Epsilona

Install the updated main ESP, Precision DLL, and particle assets together.
Requires Core Impact Framework 2.0 (the mapping uses HitContext filters).

With neither optional patch enabled, both the player and NPCs use particles.
Enable only ONE of the following, after PrecisionParticleMatch.esp:
  PrecisionParticleMatch_player_only.esp - player particles, NPC ribbons
  PrecisionParticleMatch_npc_only.esp    - NPC particles, player ribbons

The scope applies to both weapon-trail particles and impact particles, based
on the attacker, not the victim. These patches do not change combat damage.
Both patches override the same condition-only perk; if both are enabled,
the last one in the load order wins. No scripts or actor perk distribution
are needed. The updated DLL is required for weapon-trail filtering.

Original sword trails for actors excluded from particles:
Keep the supplied Precision mod and Precision Trail enchantment mod installed
in that order, then install this mod after both. Keep Precision_base.toml from
the first mod: its original enchantment conditions/colors remain in use for
ribbon actors. With no scope patch, both sides use particles. With NPC-only,
the player uses original ribbons; with player-only, NPCs use original ribbons.
Ordinary ribbons come from the first mod and enchanted ribbons from the
second mod. No additional ESP is required.
Ribbon settings preserve the supplied preset: default RGBA
(0.055, 0.216, 0.287, 1), brightness 2.6, segment lifetime 1 second and fade
1 second, with the original per-enchantment multipliers. Density patches
affect particles only and do not reduce either side's original ribbons.
The ordinary/enchanted meshes are stored separately under PlayerOriginal
(this folder name is retained for compatibility; both actor types use it).
They are unmodified copies from the two supplied mods; their original asset
authorship/licenses remain applicable. Required custom textures are included.

Only one Precision.dll is used: the rebuilt DLL supports both styles. The
supplied original DLL is 2.0.4; the build is pinned to upstream 2.0.6. Do not
rename or load the old DLL alongside the rebuilt one. Ribbon compatibility
uses the original models/configuration and retained ribbon code, not a
second DLL. Runtime compatibility still requires in-game verification.

Optional density patches (choose at most ONE, after the main ESP):
  PrecisionParticleMatch_75.esp - 75% density, 1200 particles/second
  PrecisionParticleMatch_50.esp - 50% density, 800 particles/second
  PrecisionParticleMatch_25.esp - 25% density, 400 particles/second
  PrecisionParticleMatch_5.esp  - 5% density, 80 particles/second
With none enabled, density is 100% (1600 particles/second).
These affect all included default/enchantment trail and impact particles.
Brightness, color, size, lifetime and fading are unchanged by density patches.
One density patch can be combined with one player/NPC scope patch in either
relative order. If multiple density patches are enabled, the last one wins.
The updated DLL and the Density* mesh folders are required.

Particle brightness uses a common emissive peak for every color: 2.6 before
the trail multiplier and 7.02 for impacts. The previous weighted-luminance
compensation has been replaced because it gave default blue a stronger
emissive peak than enchantment colors. Enchantments are raised to the default
particle's peak; color ratios and alpha are preserved. Runtime normalization
uses the actual trail color after overrides and only affects tagged particles.
The default trail brightness multiplier is now 5.4 (previously 3.6).
An existing MCM/user override of that setting must also be set to 5.4 to use
the new trail brightness. Perceived brightness still depends on ENB/HDR.
