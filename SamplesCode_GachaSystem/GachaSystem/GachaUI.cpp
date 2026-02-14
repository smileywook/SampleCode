/**
 * Gacha UI Implementation
 *
 * 핵심 구현 사항:
 * 1. MVVM 패턴을 통한 데이터-UI 분리
 * 2. 가챠 결과 비디오 시퀀스 자동 생성
 * 3. 네트워크 동기화를 통한 보상 처리
 */

#include "GachaUI.h"
#include "InputTriggers.h"
#include "DataTable/PlayerCharacterData.h"
#include "DataTable/RewardData.h"
#include "DataTable/VideoResourceData.h"
#include "Network/UserData_Currency.h"
#include "Network/UserData_Inventory.h"
#include "Subsystems/RewardManager.h"
#include "Subsystems/VideoPlayer.h"
#include "Subsystems/NetworkManager/NetworkManager.h"
#include "UI/ViewData/GachaViewModel.h"

void UGachaUI::Register()
{
	Super::Register();

	// 위젯에 정의된 인풋 바인딩
	BindUIInputMode();

	// 모든 비디오 재생 완료 시 콜백 등록
	UVideoPlayer::Get()->OnAllVideosEnd.AddUniqueDynamic(this, &ThisClass::OnEvent_VideoEnded);
}

/**
 * 가챠 캠페인 데이터를 ViewModel로 변환
 *
 * 로직:
 * 1. DataTable의 모든 가챠 캠페인을 순회
 * 2. 각 캠페인을 ViewModel로 변환 (캐싱 활용)
 * 3. DisplayOrder 기준으로 정렬
 * 4. UI 업데이트
 */
void UGachaUI::BuildItems()
{
	TArray<UGachaViewModel*> ViewModels;

	// DataTable Visitor 패턴 사용
	UGachaCampaignDataTable::Visit([this, &ViewModels](const FGachaCampaignData* GachaData)
	{
		const FName& RowName = GachaData->DataRowName;
		if (UGachaViewModel* ViewModel{ FindOrAddViewModel(RowName) })
		{
			ViewModel->InitializeFromData(*GachaData);
			ViewModels.Emplace(ViewModel);
		}
	});

	// UI 표시 순서 정렬
	ViewModels.Sort([](const UGachaViewModel& A, const UGachaViewModel& B)
	{
		return A.ViewData.DisplayOrder < B.ViewData.DisplayOrder;
	});

	// Blueprint에 데이터 전달
	UpdateData(ViewModels);
}

/**
 * ViewModel 캐싱 시스템
 *
 * - 불필요한 객체 생성 방지
 * - 메모리 재사용으로 GC 부담 감소
 */
UGachaViewModel* UGachaUI::FindOrAddViewModel(const FName& InRowName)
{
	TObjectPtr<UGachaViewModel>& ViewModel = CachedViewModels.FindOrAdd(InRowName);
	if (!ViewModel)
	{
		ViewModel = NewObject<UGachaViewModel>(this);
	}
	return ViewModel;
}

/**
 * Enhanced Input System 통합
 *
 * 게임패드와 키보드를 모두 지원하는 입력 매핑
 */
TArray<FWidgetInputHandler> UGachaUI::GenerateInputs() const
{
	return {
		FWidgetInputHandler(GameplayTags::Input_UI_Left_Stick_Y, ETriggerEvent::Triggered, TEXT("OnInputMove")),
		FWidgetInputHandler(GameplayTags::Input_UI_Face_Left, ETriggerEvent::Started, TEXT("OnInputPickOne")),
		FWidgetInputHandler(GameplayTags::Input_UI_Face_Top, ETriggerEvent::Started, TEXT("OnInputPickTen")),
		FWidgetInputHandler(GameplayTags::Input_UI_Face_Right, ETriggerEvent::Started, TEXT("OnInputClose")),
	};
}

/**
 * 티켓을 사용한 가챠 실행
 *
 * 프로세스:
 * 1. 로컬에서 보상 생성 (RewardManager)
 * 2. 서버에 티켓 사용 요청
 * 3. 서버 응답 후 UI 업데이트
 */
void UGachaUI::OnExecutePickup(const int32 InAmount)
{
	if (RewardGroupName.IsNone())
	{
		// 로그 : RewardGroupName is None
		return;
	}

	GachaRewards.Reset();
	GachaRewards.Reserve(InAmount);

	// 보상 생성
	FRewardHandler RewardHandler{ EReward::Gacha, RewardGroupName, InAmount };
	if (URewardManager::GiveReward(RewardHandler))
	{
		const UNetItem* NetItem = UUserData_Inventory::GetItem(CurrentTicketRowName);
		if (!NetItem)
		{
			// 로그 : Ticket NetItem is nullptr
			return;
		}

		// 서버에 티켓 사용 요청
		UNetworkManager::Request(REQ_INVENTORY_ITEM_USE, NetItem->ItemUID, PickupAmount)
		.Success(FNetworkFailDelegate::CreateLambda([this]([[maybe_unused]] const FGameAction& Action)
		{
			// 로그 : 서버 확인 완료
		}));
	}
}

/**
 * 유료 재화로 티켓 구매 후 가챠 실행
 */
void UGachaUI::OnExchangeTicket(const int32 InAmount)
{
	// 남은 티켓이 있다면 먼저 소진
	if (const UNetItem* NetItem = UUserData_Inventory::GetItem(CurrentTicketRowName))
	{
		UNetworkManager::Request(REQ_INVENTORY_ITEM_USE, NetItem->ItemUID, NetItem->Amount)
		.Success(FNetworkFailDelegate::CreateLambda([this]([[maybe_unused]] const FGameAction& Action)
		{
			// 로그 : 티켓 사용 완료
		}));
	}

	// 유료 재화 차감
	if (UUserData_Currency::GetCurrency(PrismCoinRowName))
	{
		if (URewardManager::GiveReward({ EReward::Currency, PrismCoinRowName, (InAmount * -1) }))
		{
			OnUseTicket(PickupAmount);
		}
	}
}

/**
 * 가챠 결과 비디오 시퀀스 생성
 *
 * 핵심 로직:
 * - 동적으로 비디오 재생 순서 구성
 * - 고등급 결과에 따른 특수 연출 추가
 * - 캐릭터별 인트로 비디오 자동 삽입
 *
 * @param InHandlers 가챠로 획득한 보상 목록
 * @param bGradeHigh 고등급 연출 사용 여부 (5성 이상)
 */
void UGachaUI::SetVideosToPlay(const TArray<FRewardHandler>& InHandlers, const bool bGradeHigh)
{
	if (InHandlers.IsEmpty())
	{
		return;
	}

	UVideoResourceDataTable* VideoDataTable = UVideoResourceDataTable::Get();
	if (!VideoDataTable)
	{
		return;
	}

	TArray<FVideoResourceData> VideoResources;

	// 비디오 리소스 추가 헬퍼 람다
	auto AddVideoResource = [&](const FName& InRowName, const FName& VideoName = NAME_None)
	{
		if (const FVideoResourceData* ResultData = VideoDataTable->FindRow(InRowName))
		{
			FVideoResourceData ResultDataCopy = *ResultData;
			ResultDataCopy.RootPath = FileDir;
			ResultDataCopy.VideoName = VideoName;
			VideoResources.Emplace(MoveTemp(ResultDataCopy));

			// 로그 : [GachaUI] Added Video: %s (VideoName=%s)
			return true;
		}
		return false;
	};

	// 1. 인트로 영상 (일반 또는 고등급용)
	const FName IntroName = bGradeHigh ? IntroSpecial : IntroNormal;
	AddVideoResource(IntroName);

	// 2. 결과 비디오 시퀀스 구성
	for (const FRewardHandler& Handler : InHandlers)
	{
		const URewardGachaRandomData* GachaData = URewardGachaRandomDataTable::FindRow(Handler.TypeRowName);
		if (!GachaData)
		{
			continue;
		}

		// 캐릭터 보상인 경우
		if (GachaData->Reward.RewardType == EReward::PlayerCharacter)
		{
			FName CharRowName = GachaData->Reward.TypeRowName;
			if (const FPlayerCharacterData* PlayerCharacterData = UPlayerCharacterDataTable::FindRow(CharRowName))
			{
				// 5성(전설 등급) 특수 연출
				static constexpr int32 SpecialGrade = 5;
				if (PlayerCharacterData->Grade >= SpecialGrade)
				{
					AddVideoResource(Video_5Star);
				}

				// 캐릭터 인트로 영상
				const FName CharIntroName(*(CharRowName.ToString() + IntroPrefix));
				AddVideoResource(CharIntroName);
			}
		}

		// 결과 표시 영상
		AddVideoResource(Handler.TypeRowName, Handler.TypeRowName);
	}

	// Blueprint에 비디오 재생 요청
	OnPlayVideos(VideoResources);
}

void UGachaUI::OnEvent_VideoEnded()
{
	OnVideoEnded();
}

void UGachaUI::Unregister()
{
	Super::Unregister();

	// 위젯 인풋 바인딩 해제
	UnBindUIInputMode();	

	GachaRewards.Reset();
	UVideoPlayer::Get()->OnAllVideosEnd.RemoveAll(this);
}
