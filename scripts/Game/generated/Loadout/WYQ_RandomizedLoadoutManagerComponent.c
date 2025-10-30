class WYQ_RandomizedLoadoutManagerComponentClass: BaseLoadoutManagerComponentClass
{
}

class WYQ_RandomizedLoadoutManagerComponent : BaseLoadoutManagerComponent
{
	[Attribute(defvalue:"1", params:"0 inf", desc:"Minimum amount of loot items to populate in storage")]
	int m_minLootItems;
	
	[Attribute(defvalue:"10", params:"0 inf", desc:"Maximum amount of loot items to populate in storage")]
	int m_maxLootItems;
	
	[Attribute(defvalue:"1", params:"0 inf", desc:"Minimum amount of magazines to populate in storage")]
	int m_minMagazines;
	
	[Attribute(defvalue:"5", params:"0 inf", desc:"Maximum amount of magazines to populate in storage")]
	int m_maxMagazines;
	
	string m_skipPrefabName = "SKIP";
	
	bool m_storageFull = false;
	
	void WYQ_RandomizedLoadoutManagerComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		if (!Replication.IsServer()) // only run on server, spawned items should get replicated to clients automatically
			return;
		
		SCR_ChimeraCharacter char = SCR_ChimeraCharacter.Cast(ent);
		if (char)
			GetGame().GetCallqueue().Call(ApplyRandomizedLoadout, char);
	}
	
	void ApplyRandomizedLoadout(SCR_ChimeraCharacter char)
	{
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

			if (!slotPrefab || !slotType)
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
				//GetGame().GetCallqueue().Call(StoreLoot, char, slotPrefab);
				// call loot after all items equipped
				GetGame().GetCallqueue().Call(StoreLoot, char, slotPrefab);
			} else {
				//GetGame().GetCallqueue().Call(EquipItem, char, slotPrefab, slotType);
				// call with randomized delay to avoid stampeding herd of replicatable item prefabs being spawned all at once
				GetGame().GetCallqueue().Call(EquipItem, char, slotPrefab, slotType, subItems);
			}
		}
		
		GetGame().GetCallqueue().Call(EquipWeaponAndAmmo, char);
	}
	
	void EquipItem(SCR_ChimeraCharacter char, ResourceName slotResource, LoadoutAreaType slotType, array<InventoryItemComponent> subItems)
	{
		if (!char)
			return;
		
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		SCR_CharacterInventoryStorageComponent storage = inv.GetCharacterStorage();
		EntitySpawnParams itemParams = EntitySpawnParams();
		itemParams.Parent = char;
		
		ResourceName variant = GetRandomVariant(slotResource);
		if (!storage || !variant || variant == m_skipPrefabName)
			return;

		Resource variantResource = Resource.Load(variant);
		InventoryStorageSlot slot = storage.GetSlotFromArea(slotType.Type());
		IEntity placeholder = slot.GetAttachedEntity();
		
		IEntity item = GetGame().SpawnEntityPrefab(variantResource, GetGame().GetWorld(), itemParams);
		if (!item)
			return;

		foreach (InventoryItemComponent subItem : subItems)
			inv.TryMoveItemToStorage(subItem.GetOwner(), storage.GetStorageComponentFromEntity(item));
		
		slot.AttachEntity(item);
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

			IEntity weapon = GetGame().SpawnEntityPrefab(Resource.Load(GetRandomVariant(weaponPrefab)), GetGame().GetWorld(), itemParams);
			if (!weapon)
				return;
			
			// delete slotted weapon
			SCR_EntityHelper.DeleteEntityAndChildren(slottedWeaponEntity);
			
			// replace it with randomized variant
 			inv.EquipWeapon(weapon);
			//ctrl.TryEquipRightHandItem(weapon, EEquipItemType.EEquipTypeWeapon);
			
			// add mags for selected weapon
			BaseWeaponComponent wc = BaseWeaponComponent.Cast(weapon.FindComponent(BaseWeaponComponent));
			BaseMagazineComponent currentMagazine = wc.GetCurrentMagazine();
			if (currentMagazine)
			{
				IEntity magazineEntity = currentMagazine.GetOwner();
				ResourceName resourceName = magazineEntity.GetPrefabData().GetPrefabName();
				int currentMagCount = inv.GetMagazineCountByWeapon(wc);
				
				int limit = Math.RandomInt(m_minMagazines, m_maxMagazines) - currentMagCount;
				int count;
				for (; count < limit; count++)
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
		ResourceName variant = GetRandomVariant(slotResource);
		if (!variant || variant == m_skipPrefabName)
			return true;
		
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		Resource variantResource = Resource.Load(variant);
		
		EntitySpawnParams itemParams = EntitySpawnParams();
		itemParams.Parent = char;
		
		IEntity item = GetGame().SpawnEntityPrefab(variantResource, GetGame().GetWorld(), itemParams);
		if (!item)
			return false;
		
		BaseInventoryStorageComponent storage = inv.FindStorageForItem(item, EStoragePurpose.PURPOSE_DEPOSIT);
		if (inv.CanInsertItem(item) && storage && storage.CanStoreItem(item, -1) && storage.FindSuitableSlotForItem(item))
		{
			bool insertedItem = inv.TryInsertItem(item);
			if (!insertedItem) {
				m_storageFull = true;
				SCR_EntityHelper.DeleteEntityAndChildren(item);
			}
			
			return insertedItem;
		} else {
			m_storageFull = true;
			SCR_EntityHelper.DeleteEntityAndChildren(item);
			return false;
		}
	}
	
	ResourceName GetRandomVariant(ResourceName prefab)
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
		if (variantData.GetVariants(variants) == 0)
			return prefab;
		
		//~ Created weighted array for randomization
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
			
			//~ Add to randomizer
			weightedArray.Insert(variant.m_sVariantPrefab, variant.m_iRandomizerWeight);
		}
		
		//~ No variants to randomize
		if (weightedArray.IsEmpty())
			return prefab;
		
		//~ Add default variant to randomizer
		if (variantData.m_bRandomizeDefaultVariant)
			weightedArray.Insert(prefab, variantData.m_iDefaultVariantRandomizerWeight);
		
		//~ Get random variant
		ResourceName randomVariant;
		weightedArray.GetRandomValue(randomVariant);
		
		//~ Return random variant
		return randomVariant;
	}
}