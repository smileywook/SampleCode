/**
 * Server Reward System - Gacha Implementation
 *
 * 주요 기능:
 * - 확률 기반 가챠 보상 생성
 * - 피티(Pity) 시스템 구현
 * - 픽업(Pickup) 확률 조정
 *
 * 기술 하이라이트:
 * - 가중치 기반 랜덤 알고리즘
 * - 천장(Pity) 카운터 관리
 * - 조건부 확률 증가
 */

#include "ServerRewardSystem.h"
#include "DataTable/GachaCampaignData.h"
#include "DataTable/PlayerCharacterData.h"
#include "DataTable/RewardData.h"
#include "SaveGame/ContentsAlarmSave.h"
#include "Subsystems/RewardManager.h"

/**
 * 픽업 그룹 기반 보상 선택
 *
 * 로직:
 * - 특정 PickupGroup 이상의 모든 보상 수집
 * - 랜덤하게 하나 선택
 *
 * @param RewardData 가챠 보상 데이터
 * @param PickupGroup 최소 픽업 그룹 등급
 * @return 선택된 보상
 */
FRewardHandler AddPickupReward(const URewardData* InRewardData, const int32 InPickupGroup)
{
	if (!InRewardData)
	{
		return FRewardHandler(EReward::None, NAME_None, 0);
	}

	// 조건에 맞는 보상들 수집
	TArray<const URewardGachaRandomData*> GachaRandomsData;
	for (const TObjectPtr<URewardGachaRandomData>& Data : InRewardData->GachaRandoms)
	{
		if (Data && Data->PickupGroup >= InPickupGroup)
		{
			GachaRandomsData.Add(Data);
		}
	}

	if (GachaRandomsData.IsEmpty())
	{
		// 로그 : [Gacha] No data for PickupGroup >= %d", InPickupGroup;
		return FRewardHandler(EReward::None, NAME_None, 0);
	}

	// 랜덤 선택
	const int32 Index = FMath::RandRange(0, GachaRandomsData.Num() - 1);
	FRewardHandler Reward = GachaRandomsData[Index]->Reward;

	// 로그 : [Gacha] Pickup reward: %s (PickupGroup=%d)
	return Reward;
}

/**
 * 가중치 기반 랜덤 보상 추첨
 *
 * 알고리즘:
 * 1. 1부터 총 가중치 사이의 랜덤 값 생성
 * 2. 누적 가중치가 랜덤 값을 초과하는 첫 번째 보상 선택
 *
 * 예시:
 * - 보상A (가중치 70): 1~70 범위
 * - 보상B (가중치 25): 71~95 범위
 * - 보상C (가중치 5):  96~100 범위
 *
 * @param RewardData 가챠 보상 데이터
 * @param OutPickupGroup 선택된 보상의 픽업 그룹 (출력)
 * @return 선택된 보상
 */
FRewardHandler RollRandomReward(const URewardData* InRewardData, int32& OutPickupGroup)
{
	const int32 RandomNumber{ FMath::RandRange(1, InRewardData->TotalGachaWeight) };

	int32 CurrentWeight = 0;
	for (const TObjectPtr<URewardGachaRandomData>& GachaReward : InRewardData->GachaRandoms)
	{
		CurrentWeight += GachaReward->Weight;
		if (CurrentWeight >= RandomNumber)
		{
			OutPickupGroup = GachaReward->PickupGroup;
			// 로그 : [Gacha] Random reward: %s (PickupGroup=%d, Weight=%d)
			return GachaReward->Reward;
		}
	}

	// 로그 : [Gacha] RollRandomReward failed
	return FRewardHandler(EReward::None, NAME_None, 0);
}

/**
 * 가챠 실행 (서버 측)
 *
 * 핵심 로직:
 * 1. 피티 카운터 로드
 * 2. 각 뽑기마다:
 *    a. 천장(Special Pity) 체크
 *    b. 일반 피티(10회) 체크
 *    c. 일반 랜덤 추첨
 * 3. 피티 카운터 저장
 *
 * 피티 시스템:
 * - Normal Pity: 10회마다 보장 (NormalPickupGroup 이상)
 * - Special Pity: N회마다 보장 (SpecialPickupGroup 이상)
 *
 * @param InReward 가챠 요청 정보 (보상 그룹, 뽑기 횟수)
 */
void UServerRewardSystem::OnPostGive_Gacha(const FRewardHandler* InReward)
{
	const TObjectPtr<URewardData>& RewardData{ URewardDataTable::FindRow(InReward->TypeRowName) };
	if (!RewardData)
	{
		return;
	}

	// 캠페인 데이터 조회 (피티 설정 포함)
	const FGachaCampaignData* CampaignData{ nullptr };
    UGachaCampaignDataTable::Visit([&CampaignData, &RewardData](const FGachaCampaignData* Data)
    {
        if (Data->RewardGroupRowName == RewardData->RewardGroupName)
        {
            CampaignData = Data;
        }
    });

    if (!CampaignData)
    {
	    return;
    }

	// 피티 카운터 로드
	NormalPickupCounter = 0;
	SpecialPickupCounter = 0;
	UContentsAlarmSave::GetGachaCounter(InReward->TypeRowName, NormalPickupCounter, SpecialPickupCounter);

    const int32 PickupCount = InReward->Amount;
    if (RewardData->TotalGachaWeight <= 0 || PickupCount <= 0)
    {
	    return;
    }

    TArray<FRewardHandler> RewardHandlers;
    RewardHandlers.Reserve(PickupCount);

	const int32 NormalPickupGroup = CampaignData->NormalPickupGroup;
	const int32 SpecialPickupGroup = CampaignData->SpecialPickupGroup;
	const int32 SpecialTryCount = CampaignData->SpecialTryCount;

	// 각 뽑기 실행
    for (int32 Index = 0; Index < PickupCount; ++Index)
    {
        TotalPickupCount++;
        NormalPickupCounter++;
        SpecialPickupCounter++;

        bool bSucceed = false;

		// 1. Special Pity 체크 (최고 등급 천장)
        if (SpecialPickupGroup > 0 && SpecialPickupCounter >= SpecialTryCount)
        {
        	RewardHandlers.Emplace(AddPickupReward(RewardData, SpecialPickupGroup));
            SpecialPickupCounter = 0;
            NormalPickupCounter = 0;
            bSucceed = true;
        }
		// 2. Normal Pity 체크 (10회 천장)
        else if (NormalPickupGroup > 0 && NormalPickupCounter >= 10)
        {
        	RewardHandlers.Emplace(AddPickupReward(RewardData, NormalPickupGroup));
            NormalPickupCounter = 0;
            bSucceed = true;
        }

		// 3. 일반 랜덤 추첨
        if (!bSucceed)
        {
        	int32 PickupGroup = 0;
        	FRewardHandler Reward = RollRandomReward(RewardData, PickupGroup);
        	RewardHandlers.Emplace(Reward);

			// 높은 등급 획득 시 카운터 리셋
        	if (PickupGroup >= SpecialPickupGroup)
        	{
        		SpecialPickupCounter = 0;
        		NormalPickupCounter = 0;
        	}
        	else if (PickupGroup >= NormalPickupGroup)
        	{
        		NormalPickupCounter = 0;
        	}
        }
    }

	// 보상 지급
    if (RewardHandlers.Num() == PickupCount)
    {
        URewardManager::GiveRewards(RewardHandlers);
    }

	// 피티 카운터 저장
	const int32 RemainingNormal = 10 - NormalPickupCounter;
	const int32 RemainingSpecial = SpecialTryCount - SpecialPickupCounter;

	// 로그 : [Reward_Gacha] Normal : %d/10, Special : %d/%d

	UContentsAlarmSave::SetGachaCounter(InReward->TypeRowName, NormalPickupCounter, SpecialPickupCounter);
}

/**
 * 가챠 보상 빌드 (재귀적 처리)
 *
 * RewardData 타입의 보상을 만나면 재귀적으로 전개
 * 예: 가챠 → 보상팩 → 실제 보상들
 */
bool UServerRewardSystem::BuildRewardData(const URewardData* InRewardData, TArray<FRewardHandler>& InRewardHandlers)
{
	if (!InRewardData)
	{
		return false;
	}

	// 로그 : [Reward] Build %s => StaticCount[%d] RandomCount[%d]

	InRewardHandlers.Reserve(InRewardData->Statics.Num() + InRewardData->Randoms.Num());

	auto AddReward = [&InRewardHandlers](const FRewardHandler& Handler)
	{
		// RewardData 타입은 재귀적으로 전개
		if (Handler.RewardType == EReward::RewardData)
		{
			const URewardData* StaticRewardData = URewardDataTable::FindRow(Handler.TypeRowName);

			for (int32 i = 0; i < Handler.Amount; ++i)
			{
				BuildRewardData(StaticRewardData, InRewardHandlers);
			}
		}
		else
		{
			InRewardHandlers.Emplace(RewardHandler);
		}
	};

	// 고정 보상 추가
	for (const FRewardHandler& RewardHandler : InRewardData->Statics)
	{
		AddReward(RewardHandler);
	}

	// 랜덤 보상 추첨
	if (!InRewardData->Randoms.IsEmpty())
	{
		const int32 RandomNumber{ FMath::RandRange(1, InRewardData->TotalWeight) };
		// 로그 : [Reward] %s: Random Start; %d/%d

		int32 CurrentValue = 0;
		for (const TObjectPtr<URewardRandomData>& Reward : InRewardData->Randoms)
		{
			CurrentValue += Reward->Weight;
			if (CurrentValue >= RandomNumber)
			{
				// 로그 : [Reward] Select: %s, CurrentValue: %d
				AddReward(Reward->Reward);
				break;
			}
		}
	}

	return !InRewardHandlers.IsEmpty();
}
