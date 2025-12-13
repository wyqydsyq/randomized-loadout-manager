class WYQ_LoadoutSystem : WorldSystem
{
	bool started = false;
	
	DL_LootSystem lootSystem;
	
	ref SCR_WeightedArray<SCR_EntityCatalogEntry> lootData = new SCR_WeightedArray<SCR_EntityCatalogEntry>();
	
	ref array<SCR_EArsenalItemType> labels = {
		SCR_EArsenalItemType.HEADWEAR,
		SCR_EArsenalItemType.TORSO,
		SCR_EArsenalItemType.VEST_AND_WAIST,
		SCR_EArsenalItemType.BACKPACK,
		SCR_EArsenalItemType.HANDWEAR,
		SCR_EArsenalItemType.LEGS,
		SCR_EArsenalItemType.FOOTWEAR,
	};
	
	ref array<SCR_EArsenalItemType> weaponTypes = {
		SCR_EArsenalItemType.PISTOL,
		SCR_EArsenalItemType.RIFLE,
		SCR_EArsenalItemType.SNIPER_RIFLE,
		SCR_EArsenalItemType.MACHINE_GUN
	};
	
	// map of LoadoutAreaType typename.ToString() -> weighted array
	bool loadoutDataReady = false;
	ref map<string, ref SCR_WeightedArray<SCR_EntityCatalogEntry>> loadoutData = new map<string, ref SCR_WeightedArray<SCR_EntityCatalogEntry>>();
	
	void WYQ_LoadoutSystem()
	{
		PrintFormat("WYQ_LoadoutSystem: Constructed");
	}
	
    override static void InitInfo(WorldSystemInfo outInfo)
    {
		super.InitInfo(outInfo);
        outInfo
            .SetAbstract(false)
            .SetLocation(ESystemLocation.Both)
			.AddExecuteAfter(DL_LootSystem, WorldSystemPoint.Frame)
            .AddPoint(WorldSystemPoint.FixedFrame);
    }
	
	override void OnInit()
	{
		PrintFormat("WYQ_LoadoutSystem: OnInit");
		
		if (!DL_LootSystem.GetInstance())
		{
			PrintFormat("WYQ_LoadoutSystem: Unable to find DE_LootSystem! Default placeholders will be used!", LogLevel.ERROR);
			return;
		}
	}
	
	static WYQ_LoadoutSystem GetInstance()
	{
		World world = GetGame().GetWorld();
		if (!world)
			return null;

		return WYQ_LoadoutSystem.Cast(world.FindSystem(WYQ_LoadoutSystem));
	}
	
	override void OnUpdate(ESystemPoint point)
	{
		if (!Replication.IsServer()) // only calculate updates on server, changes are broadcast to clients
			return;
		
		if (started)
			return;
		
		started = true;
		
		if (!DL_LootSystem.GetInstance().lootDataReady)
			DL_LootSystem.GetInstance().Event_LootCatalogsReady.Insert(ReadLootCatalogs);
		else
			DL_LootSystem.GetInstance().callQueue.Call(ReadLootCatalogs, DL_LootSystem.GetInstance().lootDataWeighted);
	}
	
	typename GetAreaTypeFromArsenalType(SCR_EntityCatalogEntry entry, SCR_EArsenalItemType arsenalType, SCR_EArsenalItemMode mode)
	{
		if (weaponTypes.Contains(arsenalType) && (mode == SCR_EArsenalItemMode.WEAPON || mode == SCR_EArsenalItemMode.WEAPON_VARIANTS))
			return WYQ_LoadoutWeaponArea;
		
		switch (arsenalType)
		{
			case SCR_EArsenalItemType.HEADWEAR:
			return LoadoutHeadCoverArea;
			
			case SCR_EArsenalItemType.VEST_AND_WAIST:
				Resource prefabResource = Resource.Load(entry.GetPrefab());
				IEntityComponentSource armorCompSrc = SCR_BaseContainerTools.FindComponentSource(prefabResource, SCR_ArmorDamageManagerComponent);
				if (armorCompSrc)
					return LoadoutArmoredVestSlotArea;
				return LoadoutVestArea;
			
			case SCR_EArsenalItemType.TORSO:
				return LoadoutJacketArea;
			
			case SCR_EArsenalItemType.BACKPACK:
				return LoadoutBackpackArea;
			
			case SCR_EArsenalItemType.HANDWEAR:
				return LoadoutHandwearSlotArea;
			
			case SCR_EArsenalItemType.LEGS:
				return LoadoutPantsArea;
			
			case SCR_EArsenalItemType.FOOTWEAR:
				return LoadoutBootsArea;
			
			default:
				return WYQ_LoadoutLootArea;
		}
		
		return WYQ_LoadoutLootArea;
	}
	
	ref ScriptInvoker Event_LoadoutCatalogsReady = new ScriptInvoker;
	/*
	Iterates through all loot data and filters items into separate catalogs
	based on SCR_EArsenalItemType -> LoadoutArea type,
	each LoadoutArea gets its own filtered list of items
	so when it comes to spawning items for a specific loadout area we can
	just pull from its designated list
	*/
	bool ReadLootCatalogs(SCR_WeightedArray<SCR_EntityCatalogEntry> data)
	{
		int totalCount = data.Count();
		PrintFormat("WYQ_LoadoutSystem: Reading %1 loot data entries from DynamicLoot", totalCount);
		for (int i = 0; i < totalCount - 1; i++)
		{
			float weight = data.GetWeight(i);
			SCR_EntityCatalogEntry entry = data.Get(i);
			if (!entry)
				continue;			
			
			SCR_ArsenalItem item = SCR_ArsenalItem.Cast(entry.GetEntityDataOfType(SCR_ArsenalItem));
			if (!item)
				continue;
			
			IEntityComponentSource clothCompSrc = SCR_BaseContainerTools.FindComponentSource(item.GetItemResource(), BaseLoadoutClothComponent);
			if (clothCompSrc)
			{
				string wornModel;
				clothCompSrc.Get("WornModel", wornModel);
				if (wornModel.Contains("TeamWeaponBag"))
					continue;
			}
			
			SCR_EArsenalItemType itemType = item.GetItemType();
			if (!itemType)
				continue;
			
			typename areaType = GetAreaTypeFromArsenalType(entry, itemType, item.GetItemMode());
			
			// lookup loot list for this area type or create if not yet defined
			SCR_WeightedArray<SCR_EntityCatalogEntry> areaList = loadoutData.Get(areaType.ToString());
			if (!areaList)
			{
				ref SCR_WeightedArray<SCR_EntityCatalogEntry> newEntry = new SCR_WeightedArray<SCR_EntityCatalogEntry>();
				newEntry.Insert(entry, weight);
				loadoutData.Insert(areaType.ToString(), newEntry);
			}
			else
				areaList.Insert(entry, weight);
			
			//PrintFormat("WYQ_LoadoutSystem: Populated data for loadout area %1 with %2", areaType.ToString(), entry.GetPrefab());
		}

		//DL_LootSystem.GetInstance().Event_LootCatalogsReady.Remove(ReadLootCatalogs);
		
		loadoutDataReady = true;
		
		Event_LoadoutCatalogsReady.Invoke(loadoutData);
		if (loadoutData.IsEmpty())
			return false;
		
		return true;
	}

}
