class WYQ_RandomizedLoadoutManagerComponentClass: BaseLoadoutManagerComponentClass
{
}

class WYQ_RandomizedLoadoutManagerComponent : BaseLoadoutManagerComponent
{
	[Attribute(defvalue:"0.5", params:"0 1", desc: "Chance to spawn with a backpack", category: "Randomized Loadout Manager")]
	float backpackChance;
	
	[Attribute(defvalue:"0.25", params:"0 1", desc: "Chance to spawn with an armored vest/plate carrier", category: "Randomized Loadout Manager")]
	float armorChance;
	
	[Attribute(defvalue:"5", params:"0 inf", desc: "Minimum amount of loot items to populate in storage", category: "Randomized Loadout Manager")]
	int m_minLootItems;
	
	[Attribute(defvalue:"20", params:"0 inf", desc: "Maximum amount of loot items to populate in storage", category: "Randomized Loadout Manager")]
	int m_maxLootItems;
	
	[Attribute(defvalue:"1", params:"0 inf", desc: "Minimum amount of magazines to populate in storage", category: "Randomized Loadout Manager")]
	int m_minMagazines;
	
	[Attribute(defvalue:"5", params:"0 inf", desc: "Maximum amount of magazines to populate in storage", category: "Randomized Loadout Manager")]
	int m_maxMagazines;
	
	string m_skipPrefabName = "SKIP";
	
	bool m_storageFull = false;
	
	static ref SCR_WeightedArray<SCR_EntityCatalogEntry> lootData = new SCR_WeightedArray<SCR_EntityCatalogEntry>();
	
	SCR_ChimeraCharacter char;
	IEntity weapon;
	ref array<int> attachedSlots = {};
	ref map<string, ref SCR_WeightedArray<SCR_EntityCatalogEntry>> loadoutData;
	
	void WYQ_RandomizedLoadoutManagerComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		if (!Replication.IsServer() || !GetGame().InPlayMode()) // only run on server in play mode, spawned items should get replicated to clients automatically
			return;
		
		char = SCR_ChimeraCharacter.Cast(ent);
		if (!char)
			return;
		
		WYQ_LoadoutSystem loadoutSystem = WYQ_LoadoutSystem.GetInstance();
		if (!loadoutSystem)
			return;
		
		if (loadoutSystem.loadoutDataReady)
			DL_LootSystem.GetInstance().callQueue.Call(HandleCatalogsReady, WYQ_LoadoutSystem.GetInstance().loadoutData);
		else
			loadoutSystem.Event_LoadoutCatalogsReady.Insert(HandleCatalogsReady);
	}
	
	void HandleCatalogsReady(map<string, ref SCR_WeightedArray<SCR_EntityCatalogEntry>> data)
	{
		DL_LootSystem.GetInstance().callQueue.Call(ApplyRandomizedLoadout, data);
	}
	
	void ApplyRandomizedLoadout(map<string, ref SCR_WeightedArray<SCR_EntityCatalogEntry>> data)
	{
		loadoutData = data;
		IEntityComponentSource inventoryManagerComponent = SCR_BaseContainerTools.FindComponentSource(char.GetPrefabData().GetPrefab(), WYQ_RandomizedLoadoutManagerComponent);
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		
		if (!inventoryManagerComponent || !inv)
			return;

		BaseContainerList slotList = inventoryManagerComponent.GetObjectArray("Slots");
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

			if (!slotPrefab || slotPrefab == ResourceName.Empty || !slotType || slotType.Type() == WYQ_LoadoutWeaponArea)
				continue;
			
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
				DL_LootSystem.GetInstance().callQueue.Call(StoreLoot, slotPrefab);
			else
				EquipItem(slotPrefab, slotType.Type(), subItems);
		}
		
		EquipWeaponAndAmmo();
	}
	
	void EquipItem(ResourceName slotResource, typename slotType, array<InventoryItemComponent> subItems, int attempts = 0)
	{
		
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		SCR_CharacterInventoryStorageComponent storage = inv.GetCharacterStorage();
		EntitySpawnParams itemParams = EntitySpawnParams();
		itemParams.Parent = char;
		
		ResourceName variant = GetRandomItem(slotResource, slotType.ToString());
		if (!storage || !variant || variant == m_skipPrefabName)
			return;

		Resource variantResource = Resource.Load(variant);
		InventoryStorageSlot slot = storage.GetSlotFromArea(slotType);
		if (!slot)
			return;
		
		IEntity placeholder = slot.GetAttachedEntity();
		if (!placeholder)
			return;
		
		if (
			(slotType == LoadoutBackpackArea && Math.RandomFloat(0, 1) < backpackChance)
			|| (slotType == LoadoutArmoredVestSlotArea && Math.RandomFloat(0, 1) < armorChance)
		)
		{
			slot.DetachEntity(true);
			SCR_EntityHelper.DeleteEntityAndChildren(placeholder);
			return;
		}
		
		IEntity item = GetGame().SpawnEntityPrefab(variantResource, GetGame().GetWorld(), itemParams);
		if (!item)
			return;
		
		BaseLoadoutClothComponent clothComp = BaseLoadoutClothComponent.Cast(item.FindComponent(BaseLoadoutClothComponent));
		// skip and retry on any cursed catalog entries that are not actually equippable items
		if (!clothComp)
			return EquipItem(slotResource, slotType, subItems, attempts + 1);

		foreach (InventoryItemComponent subItem : subItems)
			inv.TryMoveItemToStorage(subItem.GetOwner(), storage.GetStorageComponentFromEntity(item));

		slot.DetachEntity(false);
		
		// don't insert if slot would already be blocked e.g. armored vest with built-in rig blocks carrier rig vest slot
		if (storage.IsAreaBlocked(slotType) || !inv.CanInsertItem(item, EStoragePurpose.PURPOSE_LOADOUT_PROXY))
		{
			// re-attach placeholder and abort attempt with non-fitting item
			slot.AttachEntity(placeholder);
			SCR_EntityHelper.DeleteEntityAndChildren(item);
			
			// re-attempt with a newly selected item in hopes of selecting a non-blocking one
			if (attempts < 5)
				return EquipItem(slotResource, slotType, subItems, attempts + 1);
			
			SCR_EntityHelper.DeleteEntityAndChildren(placeholder);
			return;
		}
		
		inv.EquipAny(storage, item);
		
		if (placeholder)
			SCR_EntityHelper.DeleteEntityAndChildren(placeholder);
		
		InventoryStorageSlot armorSlot = storage.GetSlotFromArea(LoadoutArmoredVestSlotArea);
		IEntity armorPlaceholder = armorSlot.GetAttachedEntity();
		if (slotType == LoadoutVestArea && armorSlot && armorPlaceholder)
		{
			// if we equipped a plate carrier, try to equip a rig
			if (
				SCR_ArmorDamageManagerComponent.Cast(item.FindComponent(SCR_ArmorDamageManagerComponent))
				&& !storage.IsAreaBlocked(slotType)
			)
				EquipItem(slotResource, LoadoutVestArea, {}, attempts + 1);
			// if we equipped a rig, try to equip a plate carrier
			else
				EquipItem(armorPlaceholder.GetPrefabData().GetPrefabName(), LoadoutArmoredVestSlotArea, {}, attempts + 1);
		}
	}
	
	void EquipWeaponAndAmmo()
	{
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

			weapon = GetGame().SpawnEntityPrefab(Resource.Load(GetRandomItem(weaponPrefab, "WYQ_LoadoutWeaponArea")), GetGame().GetWorld(), itemParams);
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
	
	void StoreLoot(ResourceName placeholder)
	{
		if (!char)
			return;
		
		int lootCount;
		for (int lootLimit = Math.RandomInt(m_minLootItems, m_maxLootItems); lootCount < lootLimit; lootCount++)
		{
			if (!m_storageFull)
				DL_LootSystem.GetInstance().callQueue.Call(SpawnLootItem, placeholder);
		}
	}
	
	bool SpawnLootItem(ResourceName placeholderName)
	{
		if (m_storageFull)
			return true;
		
		ResourceName resourceName = GetRandomItem(placeholderName, "WYQ_LoadoutLootArea");
		if (!resourceName || resourceName == m_skipPrefabName)
			return true;
		
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		Resource resource = Resource.Load(resourceName);
		
		EntitySpawnParams itemParams = EntitySpawnParams();
		itemParams.Parent = char;
		
		IEntity item = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), itemParams);
		if (!item)
			return false;
		
		BaseInventoryStorageComponent storage = inv.FindStorageForItem(item, EStoragePurpose.PURPOSE_ANY);
		if (!storage)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(item);
			return false;
		}
		
		InventoryItemComponent invComp = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		if (weapon && invComp)
		{
			SCR_ItemAttributeCollection attr = SCR_ItemAttributeCollection.Cast(invComp.GetAttributes());
			if (attr)
			{
				WeaponAttachmentAttributes attachmentAttr = WeaponAttachmentAttributes.Cast(attr.FindAttribute(WeaponAttachmentAttributes));
				if (attachmentAttr)
				{
					SCR_WeaponAttachmentsStorageComponent attachmentStorage = SCR_WeaponAttachmentsStorageComponent.Cast(weapon.FindComponent(SCR_WeaponAttachmentsStorageComponent));
					InventoryStorageSlot slot = attachmentStorage.FindSuitableSlotForItem(item);
					if (slot)
					{
						int slotId = slot.GetID();
						
						// only attempt to attach a loot item to each slot once, subsequent items
						// for already attached slot should just be stored as loot
						if (!attachedSlots.Contains(slotId))
						{
							// try inserting attachment into an empty compatible slot
							bool insertResult = inv.TryInsertItemInStorage(item, attachmentStorage, slotId);
							if (insertResult)
							{
								attachedSlots.Insert(slotId);
								return true;
							}
							
							// try replacing default from prefab with attachment e.g. replace stock flash hider with a suppressor
							bool replaceResult = inv.TryReplaceItem(attachmentStorage, item, slotId, new SCR_InvCallBack());
							if (replaceResult)
							{
								attachedSlots.Insert(slotId);
								return true;
							}
						}
					}
				}
			}
		}
		
		bool equipped = inv.EquipAny(storage, item);
		if (!equipped && inv.CanInsertItem(item) && storage && storage.CanStoreItem(item, -1) && storage.FindSuitableSlotForItem(item))
		{
			bool insertedItem = inv.TryInsertItem(item);
			if (!insertedItem) {
				m_storageFull = true;
				SCR_EntityHelper.DeleteEntityAndChildren(item);
			}
			
			return insertedItem;
		} else if (!equipped)
			SCR_EntityHelper.DeleteEntityAndChildren(item);
		
		return true;
	}
	
	ResourceName GetRandomItem(ResourceName prefab, string type)
	{
		Resource prefabResource = Resource.Load(prefab);
		if (!prefabResource.IsValid())
			return ResourceName.Empty;
		
		IEntityComponentSource componentSource = SCR_BaseContainerTools.FindComponentSource(prefabResource, SCR_EditableEntityComponent);
		if (!componentSource)
			return GetRandomItemFromDynamicLoot(prefabResource, type);
		
		SCR_EditableEntityVariantData variantData;
		componentSource.Get("m_VariantData", variantData);
		
		if (variantData)
		{
			array<SCR_EditableEntityVariant> variants = {};
			variantData.GetVariants(variants);
			
			if (variants.Count() > 0)
				return GetRandomItemFromVariants(prefabResource, type, variants, variantData);
		}
		
		return GetRandomItemFromDynamicLoot(prefabResource, type);
	}
	
	ResourceName GetRandomItemFromVariants(Resource prefabResource, string type, array<SCR_EditableEntityVariant> variants, SCR_EditableEntityVariantData variantData)
	{
		ResourceName prefab = prefabResource.GetResource().GetResourceName();
		
		SCR_WeightedArray<string> weightedArray = new SCR_WeightedArray<string>();
		foreach (SCR_EditableEntityVariant variant : variants)
		{
			// variants with no prefab set count as chance to not spawn anything in this slot
			if (!variant.m_sVariantPrefab || variant.m_sVariantPrefab == "")
			{
				weightedArray.Insert(m_skipPrefabName, variant.m_iRandomizerWeight);
				continue;
			}
			
			Resource checkResource = Resource.Load(variant.m_sVariantPrefab);
			if (!checkResource || !checkResource.IsValid())
				continue;

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
	
	ResourceName GetRandomItemFromDynamicLoot(Resource prefabResource, string type)
	{
		ResourceName prefab = prefabResource.GetResource().GetResourceName();
		DL_LootSystem lootSystem = DL_LootSystem.GetInstance();
		if (!lootSystem)
			return ResourceName.Empty;
		
		SCR_WeightedArray<SCR_EntityCatalogEntry> slotCatalog = lootSystem.lootDataWeighted;
		
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
}