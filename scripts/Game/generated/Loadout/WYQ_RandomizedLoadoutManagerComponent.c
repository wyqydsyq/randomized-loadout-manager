class WYQ_RandomizedLoadoutManagerComponentClass: BaseLoadoutManagerComponentClass
{
}

class WYQ_RandomizedLoadoutManagerComponent : BaseLoadoutManagerComponent
{
	[Attribute(defvalue:"1", params:"0 inf", desc:"Minimum amount of loot items to populate in storage")]
	int m_minLootItems;
	
	[Attribute(defvalue:"10", params:"0 inf", desc:"Maximum amount of loot items to populate in storage")]
	int m_maxLootItems;
	
	string m_skipPrefabName = "SKIP";
	
	void WYQ_RandomizedLoadoutManagerComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		if (!Replication.IsServer()) // only run on server, spawned items should get replicated to clients automatically
			return;
		
		SCR_ChimeraCharacter char = SCR_ChimeraCharacter.Cast(ent);
		if (char)
			DressNPC(src, char);
	}
	
	void DressNPC(IEntityComponentSource src, SCR_ChimeraCharacter char)
	{
		IEntitySource entitySource = src.ToEntitySource();
		IEntityComponentSource inventoryManagerComponent = SCR_BaseContainerTools.FindComponentSource(char.GetPrefabData().GetPrefab(), WYQ_RandomizedLoadoutManagerComponent);
		
		if (!inventoryManagerComponent)
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
			if (!slotPrefab)
				continue;
			
			if (slot.GetName() == "Loot")
			{
				GetGame().GetCallqueue().Call(StoreLoot, char, slotPrefab);
			} else {
				GetGame().GetCallqueue().Call(EquipItem, char, slotPrefab);
			}
		}
		
		GetGame().GetCallqueue().Call(EquipWeaponAndAmmo, char);
	}
	
	void EquipItem(SCR_ChimeraCharacter char, ResourceName slotResource)
	{
		if (!char)
			return;
		
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		EntitySpawnParams itemParams = EntitySpawnParams();
		itemParams.Parent = char;
		
		ResourceName variant = GetRandomVariant(slotResource);
		if (!variant || variant == m_skipPrefabName)
		{
			// remove base prefab when skipping a slot
			IEntity temp = GetGame().SpawnEntityPrefab(Resource.Load(slotResource), GetGame().GetWorld(), itemParams);
			InventoryStorageSlot slot = inv.GetCharacterStorage().FindSuitableSlotForItem(temp);
			if (!slot)
			{
				SCR_EntityHelper.DeleteEntityAndChildren(temp);
				return;
			}
			
			IEntity currentEntity = slot.GetAttachedEntity();
			if (currentEntity)
			{
				slot.DetachEntity(true);
				SCR_EntityHelper.DeleteEntityAndChildren(currentEntity);
				SCR_EntityHelper.DeleteEntityAndChildren(temp);
			}
			return;
		}
		
		
		Resource variantResource = Resource.Load(variant);
		IEntity item = GetGame().SpawnEntityPrefab(variantResource, GetGame().GetWorld(), itemParams);
		
		InventoryStorageSlot slot = inv.GetCharacterStorage().FindSuitableSlotForItem(item);
		if (!slot)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(item);
			return;
		}
		
		IEntity currentEntity = slot.GetAttachedEntity();
		if (currentEntity)
		{
			slot.DetachEntity(true);
			SCR_EntityHelper.DeleteEntityAndChildren(currentEntity);
		}
		
		inv.EquipAny(inv.GetCharacterStorage(), item);
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

			IEntity weapon = GetGame().SpawnEntityPrefab(Resource.Load(GetRandomVariant(weaponPrefab)), GetGame().GetWorld(), itemParams);
			
			// delete slotted weapon
			SCR_EntityHelper.DeleteEntityAndChildren(slottedWeaponEntity);
			
			// replace it with randomized variant
 			inv.EquipWeapon(weapon);
			ctrl.TryEquipRightHandItem(weapon, EEquipItemType.EEquipTypeWeapon);
			
			// add mags for selected weapon
			BaseWeaponComponent wc = BaseWeaponComponent.Cast(weapon.FindComponent(BaseWeaponComponent));
			BaseMagazineComponent currentMagazine = wc.GetCurrentMagazine();
			if (currentMagazine)
			{
				IEntity magazineEntity = currentMagazine.GetOwner();
				ResourceName resourceName = magazineEntity.GetPrefabData().GetPrefabName();
				int currentMagCount = inv.GetMagazineCountByWeapon(wc);
				
				int limit = Math.RandomInt(2, 6 - currentMagCount);
				int count;
				for (; count < limit; count++)
				{
					IEntity mag = GetGame().SpawnEntityPrefab(Resource.Load(resourceName), GetGame().GetWorld(), itemParams);
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
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(char.FindComponent(SCR_InventoryStorageManagerComponent));
		
		EntitySpawnParams itemParams = EntitySpawnParams();
		itemParams.Parent = char;
		
		int lootCount;
		for (int lootLimit = Math.RandomInt(m_minLootItems, m_maxLootItems); lootCount < lootLimit; lootCount++)
		{
			// get random variant from slotted resource
			Resource variantResource = Resource.Load(GetRandomVariant(slotResource));
			IEntity item = GetGame().SpawnEntityPrefab(variantResource, GetGame().GetWorld(), itemParams);
			BaseInventoryStorageComponent storage = inv.FindStorageForItem(item);
			
			if (inv.CanInsertItem(item) && storage && storage.CanStoreItem(item, -1) && inv.TryInsertItem(item))
			{} else {
				SCR_EntityHelper.DeleteEntityAndChildren(item);
				break;
			}
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
			if (!variant.m_sVariantPrefab)
			{
				weightedArray.Insert(m_skipPrefabName, variant.m_iRandomizerWeight);
				continue;
			}
			
			prefabResource = Resource.Load(variant.m_sVariantPrefab);
			if (!prefabResource.IsValid())
				continue;
			
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