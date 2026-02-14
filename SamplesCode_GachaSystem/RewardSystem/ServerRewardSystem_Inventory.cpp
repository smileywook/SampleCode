/**
 * Server Reward System - Inventory Management
 *
 * 주요 기능:
 * - 스택 가능/불가능 아이템 처리
 * - 인벤토리 용량 체크
 * - 아이템 병합 최적화
 *
 * 기술 하이라이트:
 * - 스마트 스택 병합 알고리즘
 * - 선제적 용량 검증
 * - 트랜잭션 기반 일관성 보장
 */

#include "ServerRewardSystem.h"
#include "Common/SqliteUtil.h"
#include "DataTable/ItemDataTable.h"
#include "DataTable/ItemToolData.h"
#include "DataTable/ItemValuableData.h"
#include "Network/UserData_Equipment.h"
#include "Network/UserData_Inventory.h"

/**
 * 보상 시뮬레이션 (실제 지급 전 검증)
 *
 * 목적:
 * - 인벤토리 용량 초과 방지
 * - 스택 병합으로 슬롯 최적화
 * - 트랜잭션 롤백 방지
 *
 * 알고리즘:
 * 1. 스택 가능 아이템 병합
 * 2. 현재 인벤토리 슬롯 수 계산
 * 3. 추가될 슬롯 수 예측
 * 4. 최대 용량 초과 여부 확인
 */
bool UServerRewardSystem::SimulateRewards(TArray<FRewardHandler>& InRewards, const TArray<UNetItem*>& InUpdatedItem, const bool bCheckInventory/* = true*/)
{
	const ERewardSource DefaultSource = InRewards.Num() > 0 ? InRewards[0].AcquireSource : ERewardSource::None;

    TArray<FRewardHandler> NonStackablePass;
    TMap<FName, int32> StackableCounts;

	// 1. 아이템 분류 및 병합
    for (int32 i = 0; i < InRewards.Num(); ++i)
    {
        const FRewardHandler& Reward = InRewards[i];
        if (Reward.RewardType != EReward::Item)
		{
            continue;
		}

        const FItemBaseData* ItemData = UItemDataTable::FindRow<FItemBaseData>(Reward.TypeRowName);
        if (!ItemData)
        {
            // 로그 : ItemData not found: %s
            continue;
        }

		// 스택 불가능 아이템 (무기, 방어구 등)
        if (ItemData->IsNonStackable())
        {
            NonStackablePass.Add(Reward);
            InRewards.RemoveAt(i--, EAllowShrinking::No);
        }
		// 스택 가능 아이템 (소비 아이템 등) - 병합
        else
        {
            StackableCounts.FindOrAdd(Reward.TypeRowName) += Reward.Amount;
            InRewards.RemoveAt(i--, EAllowShrinking::No);
        }
    }

	// 2. 병합된 아이템 재구성
    for (const auto& Pair : StackableCounts)
    {
        const FName ItemRowName = Pair.Key;
        const int32 Amount = Pair.Value;
        if (Amount != 0)
        {
            InRewards.Emplace(EReward::Item, ItemRowName, Amount, DefaultSource);
        }
    }
    InRewards.Append(NonStackablePass);

	// 3. 현재 인벤토리 슬롯 계산
    int32 SlotAmount = UUserData_Inventory::GetItemSlotCount();

	// 4. 업데이트될 아이템의 슬롯 영향 계산
    for (const auto& It : InUpdatedItem)
    {
        if (!It || !It->ItemData)
        {
	        continue;
        }
        if (!It->ItemData->RequiresInventorySlot())
        {
	        continue;
        }

		// 스택 불가능 아이템
        if (It->ItemData->IsNonStackable())
        {
            if (It->Amount > 0)
            {
	            SlotAmount += It->Amount;  // 추가
            }
            else if (It->Amount < 0)
            {
	            SlotAmount -= FMath::Abs(It->Amount);  // 제거
            }
        }
		// 스택 가능 아이템
        else
        {
            SlotAmount += (It->Amount > 0 ? 1 : -1);
        }
    }

    const int32 MaxCapacity = UUserData_Inventory::GetMaxCapacity();

	// 5. 추가될 아이템의 슬롯 영향 예측
    for (const FRewardHandler& Reward : InRewards)
    {
        if (!URewardManager::Simulate(&Reward))
        {
            // 로그 : %s Simulate Fail
            return false;
        }

        if (!bCheckInventory)
        {
        	continue;
        }

        if (Reward.RewardType != EReward::Item || Reward.AcquireSource != ERewardSource::None)
        {
        	continue;
        }

        const FItemBaseData* ItemData = UItemDataTable::FindRow<FItemBaseData>(Reward.TypeRowName);
        if (!ItemData)
        {
            // 로그 : ItemData not found: %s
            continue;
        }

        if (!ItemData->RequiresInventorySlot())
        {
        	continue;
        }

        const int32 UserAmount = UUserData_Inventory::GetAmount(Reward.TypeRowName);

		// 스택 불가능 아이템
        if (ItemData->IsNonStackable())
        {
            if (Reward.Amount > 0)
            {
                SlotAmount += Reward.Amount;
            }
            else if (Reward.Amount < 0)
            {
                const int32 Decrease = FMath::Min(UserAmount, -Reward.Amount);
                SlotAmount -= Decrease;
            }
        }
		// 스택 가능 아이템
        else
        {
            if (Reward.Amount > 0)
            {
				// 새로운 아이템이면 슬롯 +1
                if (UserAmount == 0)
                {
                    ++SlotAmount;
                }
				// 기존 아이템에 스택 (슬롯 변화 없음)
            }
            else if (Reward.Amount < 0)
            {
				// 아이템이 완전히 소진되면 슬롯 -1
                if (UserAmount + Reward.Amount <= 0)
                {
                    --SlotAmount;
                }
            }
        }

		// 6. 용량 초과 체크
        if (SlotAmount > MaxCapacity)
        {
            // 로그 : Inventory Full;
            return false;
        }
    }

    return true;
}

/**
 * 인벤토리 아이템 추가
 *
 * 로직:
 * - 스택 가능: 기존 아이템에 수량 추가
 * - 스택 불가능: 새 아이템 생성
 * - 서브 옵션 자동 생성 (장비 아이템)
 */
UNetItem* UServerRewardSystem::AddInventoryItem(const int32 InItemID, const int32 InAddAmount, FSqliteQueryTask* InTask)
{
	const FItemBaseData* ItemData{ UItemDataTable::FindRow(InItemID) };
	const bool bCanStack = ItemData->MaxStackAmount > 1;
	UNetItem* NetItem = bCanStack ? DuplicateNetItemByID(InItemID) : nullptr;

	// 스택 가능하고 기존 아이템이 있으면 수량만 증가
	if (NetItem && bCanStack)
	{
		OnUpdateItemAmount(NetItem, InAddAmount, InTask);
		return NetItem;
	}

	// 새 아이템 생성
	NetItem = NewObject<UNetItem>(this);
	NetItem->ItemID = InItemID;
	NetItem->Amount = InAddAmount;
	NetItem->ItemData = ItemData;
	NetItem->ItemUID = Sqlite::QueryGameDB(SqlGameQuery::InsertItem, AccountID, NetItem->ItemID, NetItem->Amount)->GetLastInsertRowId();
	NetItem->CreateDate = FDateTime::Now();

	InTask->AddQuery(SqlGameQuery::InsertInventory, AccountID, NetItem->ItemUID);

	// 장비 아이템이면 서브 옵션 생성
	BuildOptions(NetItem, InTask);
	return NetItem;
}

/**
 * 인벤토리 아이템 제거
 */
bool UServerRewardSystem::RemoveInventoryItem(UNetItem* InNetItem, const int32 InRemoveAmount, FSqliteQueryTask* InTask)
{
	if (!InNetItem || InRemoveAmount < 0 || InNetItem->Amount - InRemoveAmount < 0)
	{
		return false;
	}

	OnUpdateItemAmount(InNetItem, InRemoveAmount * -1, InTask);

	// 아이템이 완전히 소진되면 DB에서 삭제
	if (InNetItem->Amount <= 0)
	{
		InTask->AddQuery(SqlGameQuery::DeleteInventory, InNetItem->ItemUID);

		// 장착중인 장비라면 장착 정보도 삭제
		if (UUserData_Equipment::GetEquipmentBy(InNetItem->ItemID) != nullptr)
		{
			InTask->AddQuery(SqlGameQuery::DeleteEquipment, Instance->AccountID, InNetItem->ItemUID);
		}
	}
	return true;
}

/**
 * 아이템 수량 업데이트
 */
void UServerRewardSystem::OnUpdateItemAmount(UNetItem* InNetItem, const int32 InUpdateAmount, FSqliteQueryTask* Task)
{
	const FItemBaseData* ItemData{ UItemDataTable::FindRow(InNetItem->ItemID) };
	const int32 PreAmount = InNetItem->Amount;
	InNetItem->Amount = FMath::Clamp(PreAmount + InUpdateAmount, 0, ItemData->MaxStackAmount);

	if (InNetItem->Amount > 0)
	{
		Task->AddQuery(SqlGameQuery::UpdateItemAmount, InNetItem->Amount, InNetItem->ItemUID);
	}
	else
	{
		Task->AddQuery(SqlGameQuery::DeleteItem, InNetItem->ItemUID);
	}
}

/**
 * 장비 서브 옵션 생성
 *
 * 장비 아이템에 랜덤 옵션 부여:
 * - 공격력 증가
 * - 방어력 증가
 * - 크리티컬 확률
 * 등
 */
void UServerRewardSystem::BuildOptions(UNetItem* InNetItem, FSqliteQueryTask* InTask, const TArray<TObjectPtr<UNetItemOption>>* InFixedOptions/* = nullptr*/)
{
	if (InNetItem->GetItemType() != EItem::Equip)
	{
		return;
	}

	TArray<TObjectPtr<UEquipmentSubOptionData>> Options;
	UEquipmentSubOptionDataTable::BuildOptions(static_cast<const FEquipmentData*>(InNetItem->ItemData), Options);
	InNetItem->Options.Empty(Options.Num());

	for (int32 i = 0; i < Options.Num(); ++i)
	{
		// 고정 옵션이 있으면 사용 (인챈트 시스템 등)
		if (TObjectPtr FixedOption{ InFixedOptions && InFixedOptions->IsValidIndex(i) ? InFixedOptions->operator[](i) : nullptr })
		{
			InNetItem->Options.Emplace(FixedOption);
			continue;
		}

		// 랜덤 옵션 생성
		const TObjectPtr<UEquipmentSubOptionData> Option = Options[i];
		UNetItemOption* ItemOption = NewObject<UNetItemOption>(this);
		ItemOption->OptionID = Option->GetEffectRowID();
		ItemOption->OptionValue = Option->EffectValue;
		InTask->AddQuery(SqlGameQuery::InsertItemOption, AccountID, NetItem->ItemUID, Option->GetEffectRowID(), Option->EffectValue);
		NetItem->Options.Emplace(ItemOption);
	}
}
