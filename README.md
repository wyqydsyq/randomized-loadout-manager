![Wyq's Randomized Loadout Manager](preview.png "Wyq's Randomized Loadout Manager")

# Wyq's Randomized Loadout Manager

Originally created to implement Scavs in [Mercenary Outlands](https://reforger.armaplatform.com/workshop/64C4F3E0169E5739-MercenaryOutlands), refactored to be more general-purpose and released standalone to give something back to the modding community :)

## Setup
See `WYQ_TestWorld.ent` and/or `Randomized_Character_Base.et` for an example.

### Creating a new randomized character prefab

 - Replace `BaseLoadoutManagerComponent` with `WYQ_RandomizedLoadoutManagerComponent` in your prefab's components.
 - Populate loadout slots with randomized item prefabs (examples in `Prefabs/Characters/WYQ_RandomizedLoadoutManager`).
 - Populate primary `CharacterWeaponSlotComponent` with randomized weapon prefab (variants can include secondary slot weapons i.e. handguns and they should get equipped correctly, this is just somewhere to set the prefab).

### Randomization behaviour

 - Uses vanilla reforger `SCR_EditableEntityComponent` weighted variant logic for randomization (makes it easy to configure in WB!)
 - Any variants with `NO PREFAB` will count as a chance to not spawn anything in that slot (e.g. chance to not have a backpack)


## License
APL - https://www.bohemia.net/community/licenses/arma-public-license