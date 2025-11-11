class WYQ_RandomizedLoadoutManagerComponentClass: BaseLoadoutManagerComponentClass
{
}

class WYQ_RandomizedLoadoutManagerComponent : BaseLoadoutManagerComponent
{
	[Attribute(defvalue:"5", params:"0 inf", desc:"Minimum amount of loot items to populate in storage")]
	int m_minLootItems;
	
	[Attribute(defvalue:"20", params:"0 inf", desc:"Maximum amount of loot items to populate in storage")]
	int m_maxLootItems;
	
	[Attribute(defvalue:"1", params:"0 inf", desc:"Minimum amount of magazines to populate in storage")]
	int m_minMagazines;
	
	[Attribute(defvalue:"5", params:"0 inf", desc:"Maximum amount of magazines to populate in storage")]
	int m_maxMagazines;
	
	string m_skipPrefabName = "SKIP";
	
	bool m_storageFull = false;
	
	static ref SCR_WeightedArray<SCR_EntityCatalogEntry> lootData = new SCR_WeightedArray<SCR_EntityCatalogEntry>();
	
	SCR_ChimeraCharacter char;
	ref map<string, ref SCR_WeightedArray<SCR_EntityCatalogEntry>> loadoutData;
	
	void WYQ_RandomizedLoadoutManagerComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		if (!Replication.IsServer() || !GetGame().InPlayMode()) // only run on server in play mode, spawned items should get replicated to clients automatically
			return;
		
		char = SCR_ChimeraCharacter.Cast(ent);
		
		if (WYQ_LoadoutSystem.GetInstance().loadoutDataReady)
			HandleCatalogsReady(WYQ_LoadoutSystem.GetInstance().loadoutData);
		else
			WYQ_LoadoutSystem.GetInstance().Event_LoadoutCatalogsReady.Insert(HandleCatalogsReady);
	}
	
	void HandleCatalogsReady(map<string, ref SCR_WeightedArray<SCR_EntityCatalogEntry>> data)
	{
		GetGame().GetCallqueue().Call(ApplyRandomizedLoadout, data);
	}
	
	void ApplyRandomizedLoadout(map<string, ref SCR_WeightedArray<SCR_EntityCatalogEntry>> data)
	{
		loadoutData = data;
		IEntityComponentSource inventoryManagerComponent = SCR_BaseContainerTools.FindComponentSource(char.GetPrefabData().GetPrefab(), WYQ_RandomizedLoadoutManagerComponent);
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		
		if (!inventoryManagerComponent || !inv)
			return;

		BaseContainerList slotList = BaseContainerList.Cast(inventoryManagerComponent.GetObjectArray("Slots"));
		if (!slotList)
			return;
		
		// randomize slot variants
		for (int i, count = slotList.Count(); i < count; i++)
		{
			BaseContainer slot = slotList.Get(i);
			ResourceName slotPrefab;
			slot.Get("Prefab", slotPrefab);
			LoadoutAreaType slotType;
			slot.Get("AreaType", slotType);

			if (!slotPrefab || !slotType || slotType.Type() == WYQ_LoadoutWeaponArea)
				continue;
			
			// delete slot placeholder to create space for randomized variant
			SCR_CharacterInventoryStorageComponent storage = inv.GetCharacterStorage();
			InventoryStorageSlot itemSlot = storage.GetSlotFromArea(slotType.Type());
			IEntity placeholder = itemSlot.GetAttachedEntity();
			
			// skip replacing non-placeholder entities to avoid swapping out persisted or non-random gear
			if (!placeholder || placeholder && placeholder.GetPrefabData().GetPrefabName() != slotPrefab)
				continue;
			
			array<InventoryItemComponent> subItems = {};
			SCR_UniversalInventoryStorageComponent placeholderStorage = storage.GetStorageComponentFromEntity(placeholder);
			if (placeholderStorage)
				placeholderStorage.GetOwnedItems(subItems);
			
			if (slotType.Type() == WYQ_LoadoutLootArea)
			{
				// call loot after all items equipped
				GetGame().GetCallqueue().Call(StoreLoot, char, slotPrefab);
			} else {
				EquipItem(char, slotPrefab, slotType.Type(), subItems);
			}
		}
		
		GetGame().GetCallqueue().Call(EquipWeaponAndAmmo, char);
	}
	
	void EquipItem(SCR_ChimeraCharacter char, ResourceName slotResource, typename slotType, array<InventoryItemComponent> subItems, int attempts = 0)
	{
		if (!char)
			return;
		
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		SCR_CharacterInventoryStorageComponent storage = inv.GetCharacterStorage();
		EntitySpawnParams itemParams = EntitySpawnParams();
		itemParams.Parent = char;
		
		ResourceName variant = GetRandomVariantFromDynamicLoot(slotResource, slotType.ToString());
		if (!storage || !variant || variant == m_skipPrefabName)
			return;

		Resource variantResource = Resource.Load(variant);
		InventoryStorageSlot slot = storage.GetSlotFromArea(slotType);
		if (!slot)
			return;
		
		IEntity placeholder = slot.GetAttachedEntity();
		if (!placeholder)
			return;
		
		IEntity item = GetGame().SpawnEntityPrefab(variantResource, GetGame().GetWorld(), itemParams);
		if (!item)
			return;

		foreach (InventoryItemComponent subItem : subItems)
			inv.TryMoveItemToStorage(subItem.GetOwner(), storage.GetStorageComponentFromEntity(item));

		slot.DetachEntity(false);
		
		// don't insert if slot would already be blocked e.g. armored vest with rig blocks rig vest slot
		if (storage.IsAreaBlocked(slotType) || !inv.CanInsertItem(item, EStoragePurpose.PURPOSE_LOADOUT_PROXY))
		{
			// re-attach placeholder and abort attempt with non-fitting item
			slot.AttachEntity(placeholder);
			SCR_EntityHelper.DeleteEntityAndChildren(item);
			
			// re-attempt with a newly selected item in hopes of selecting a non-blocking one
			if (attempts < 5)
				return EquipItem(char, slotResource, slotType, subItems, attempts + 1);
			
			SCR_EntityHelper.DeleteEntityAndChildren(placeholder);
			return;
		}
		
		// if equipped an armored vest, queue another equip attempt for fitting rig vest
		if (slotType == LoadoutVestArea && SCR_ArmorDamageManagerComponent.Cast(item.FindComponent(SCR_ArmorDamageManagerComponent)))
		{
			slot.AttachEntity(placeholder);
			EquipItem(char, slotResource, LoadoutVestArea, subItems, attempts + 1);
			if (placeholder)
				slot.DetachEntity();
			
			InventoryStorageSlot armorSlot = storage.GetSlotFromArea(LoadoutArmoredVestSlotArea);
			armorSlot.AttachEntity(item);
		}
		else
			inv.EquipAny(storage, item);
		
		if (placeholder)
			SCR_EntityHelper.DeleteEntityAndChildren(placeholder);
	}

	void HandleReplaceItem()
	{
		
	}
	
	void EquipWeaponAndAmmo(SCR_ChimeraCharacter char)
	{
		if (!char)
			return;
		
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		CharacterControllerComponent ctrl = char.GetCharacterController();
		BaseWeaponManagerComponent wm = char.GetWeaponManager();
		if (!wm)
			return;
		
		EntitySpawnParams itemParams = EntitySpawnParams();
		itemParams.Parent = char;
		
		BaseWeaponComponent slottedWeaponComponent = wm.GetCurrentWeapon();
		if (slottedWeaponComponent)
		{
			IEntity slottedWeaponEntity = slottedWeaponComponent.GetOwner();
			ResourceName weaponPrefab = slottedWeaponComponent.GetOwner().GetPrefabData().GetPrefabName();
			
			// skip replacing non-placeholder entities to avoid swapping out persisted or non-random gear
			if (!slottedWeaponEntity || slottedWeaponEntity && slottedWeaponEntity.GetPrefabData().GetPrefabName() != weaponPrefab)
				return;

			IEntity weapon = GetGame().SpawnEntityPrefab(Resource.Load(GetRandomVariantFromDynamicLoot(weaponPrefab, "WYQ_LoadoutWeaponArea")), GetGame().GetWorld(), itemParams);
			if (!weapon)
				return;
			
			BaseWeaponComponent wc = BaseWeaponComponent.Cast(weapon.FindComponent(BaseWeaponComponent));
			if (!wc)
			{
				PrintFormat("RLM: EquipWeaponAndAmmo: %1 does not have BaseWeaponComponent!", wc, LogLevel.ERROR);
				return;
			}
			
			// delete slotted weapon
			SCR_EntityHelper.DeleteEntityAndChildren(slottedWeaponEntity);
			
			// replace it with randomized variant
 			inv.EquipWeapon(weapon);
			ctrl.TryEquipRightHandItem(weapon, EEquipItemType.EEquipTypeWeapon);
			
			// add mags for selected weapon
			BaseMagazineComponent currentMagazine = wc.GetCurrentMagazine();
			if (currentMagazine)
			{
				IEntity magazineEntity = currentMagazine.GetOwner();
				ResourceName resourceName = magazineEntity.GetPrefabData().GetPrefabName();
				int currentMagCount = inv.GetMagazineCountByWeapon(wc);
				
				int limit = Math.RandomInt(m_minMagazines, m_maxMagazines) - currentMagCount;
				int count;
				for (; count <= limit; count++)
				{
					IEntity mag = GetGame().SpawnEntityPrefab(Resource.Load(resourceName), GetGame().GetWorld(), itemParams);
					if (!mag)
						return;
					
					BaseInventoryStorageComponent storage = inv.FindStorageForItem(mag);
					
					if (inv.CanInsertItem(mag) && storage && storage.CanStoreItem(mag, -1) && inv.TryInsertItem(mag))
					{} else {
						SCR_EntityHelper.DeleteEntityAndChildren(mag);
						break;
					}
				}
			}
		}
	}
	
	void StoreLoot(SCR_ChimeraCharacter char, ResourceName slotResource)
	{
		if (!char)
			return;
		
		int lootCount;
		for (int lootLimit = Math.RandomInt(m_minLootItems, m_maxLootItems); lootCount < lootLimit; lootCount++)
		{
			if (!m_storageFull)
				GetGame().GetCallqueue().Call(SpawnLootItem, char, slotResource);
		}
	}
	
	bool SpawnLootItem(SCR_ChimeraCharacter char, ResourceName slotResource)
	{
		if (m_storageFull)
			return true;
		
		// get random variant from slotted resource
		ResourceName variant = GetRandomVariantFromDynamicLoot(slotResource, "WYQ_LoadoutLootArea");
		if (!variant || variant == m_skipPrefabName)
			return true;
		
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		Resource variantResource = Resource.Load(variant);
		
		EntitySpawnParams itemParams = EntitySpawnParams();
		itemParams.Parent = char;
		
		IEntity item = GetGame().SpawnEntityPrefab(variantResource, GetGame().GetWorld(), itemParams);
		if (!item)
			return false;
		
		BaseInventoryStorageComponent storage = inv.FindStorageForItem(item, EStoragePurpose.PURPOSE_ANY);
		if (!storage)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(item);
			return false;
		}
		
		if (inv.CanInsertItem(item) && storage && storage.CanStoreItem(item, -1) && storage.FindSuitableSlotForItem(item))
		{
			bool insertedItem = inv.TryInsertItem(item);
			if (!insertedItem) {
				m_storageFull = true;
				SCR_EntityHelper.DeleteEntityAndChildren(item);
			}
			
			return insertedItem;
		} else {
			SCR_EntityHelper.DeleteEntityAndChildren(item);
			return false;
		}
	}
	
	ResourceName GetRandomVariantFromDynamicLoot(ResourceName prefab, string type)
	{
		Resource prefabResource = Resource.Load(prefab);
		if (!prefabResource.IsValid())
			return prefab;
		
		IEntityComponentSource componentSource = SCR_BaseContainerTools.FindComponentSource(prefabResource, SCR_EditableEntityComponent);
		if (!componentSource)
			return prefab;
		
		SCR_EditableEntityVariantData variantData;
		componentSource.Get("m_VariantData", variantData);
		
		if (!variantData)
			return prefab;

		array<SCR_EditableEntityVariant> variants = {};
		variantData.GetVariants(variants);
		if (DL_LootSystem.GetInstance()) // && variants.Count() == 0)
		{
			SCR_WeightedArray<SCR_EntityCatalogEntry> slotCatalog = DL_LootSystem.GetInstance().lootData;
			
			// filter to specific type if specified
			if (type && loadoutData && loadoutData.Count() > 0)
				slotCatalog = loadoutData.Get(type);
			
			SCR_EntityCatalogEntry entry;
			if (slotCatalog)
				slotCatalog.GetRandomValue(entry);
			if (entry)
				return entry.GetPrefab();
			
			return prefab;
		}
		
		// DynamicLoot instance not found or manual variants specified for slot, proceed with vanilla variant randomization 
		SCR_WeightedArray<string> weightedArray = new SCR_WeightedArray<string>();
		foreach (SCR_EditableEntityVariant variant : variants)
		{
			// variants with no prefab set count as chance to not spawn anything in this slot
			if (!variant.m_sVariantPrefab || variant.m_sVariantPrefab == "")
			{
				weightedArray.Insert(m_skipPrefabName, variant.m_iRandomizerWeight);
				continue;
			}
			
			prefabResource = Resource.Load(variant.m_sVariantPrefab);
			if (!prefabResource.IsValid())
			{
				weightedArray.Insert(m_skipPrefabName, variant.m_iRandomizerWeight);
				continue;
			}

			weightedArray.Insert(variant.m_sVariantPrefab, variant.m_iRandomizerWeight);
		}

		if (weightedArray.IsEmpty())
			return prefab;

		if (variantData.m_bRandomizeDefaultVariant)
			weightedArray.Insert(prefab, variantData.m_iDefaultVariantRandomizerWeight);

		ResourceName randomVariant;
		weightedArray.GetRandomValue(randomVariant);
		return randomVariant;
	}
}