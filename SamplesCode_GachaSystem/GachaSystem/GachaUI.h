/**
 * Gacha UI System
 *
 * 주요 기능:
 * - MVVM 패턴을 활용한 가챠 UI 관리
 * - 멀티 가챠 캠페인 지원
 * - 티켓 및 재화 기반 가챠 실행
 * - 비디오 연출 통합
 */

#pragma once

#include "CoreMinimal.h"
#include "UI/UIScreen.h"
#include "GachaUI.generated.h"

struct FInputActionValue;
struct FVideoPlayHandler;
struct FVideoResourceData;
class UFileMediaSource;
class UGachaViewModel;

/**
 * 가챠 시스템의 메인 UI 스크린 클래스
 *
 * 주요 역할:
 * - 가챠 캠페인 데이터를 ViewModel로 변환하여 UI에 표시
 * - 사용자 입력 처리 (1회/10회 뽑기, 티켓 교환)
 * - 가챠 결과 비디오 연출 관리
 * - 서버와의 보상 동기화
 */
UCLASS()
class UGachaUI : public UUIScreen
{
	GENERATED_BODY()

protected:
	// 가챠 보상 그룹 식별자
	UPROPERTY(BlueprintReadWrite)
	FName RewardGroupName{ NAME_None };

	// 뽑기 횟수 (1회 또는 10회)
	UPROPERTY(BlueprintReadWrite)
	int32 PickupAmount{ 0 };

	// 유료 재화 식별자
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FName PrismCoinRowName{ NAME_None };

	// 현재 사용중인 티켓 식별자
	UPROPERTY(BlueprintReadWrite)
	FName CurrentTicketRowName{ NAME_None };

	// UI 사운드 설정
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIConfig|Sound")
    FDataTableRowHandle InputMoveSound;

private:
	// 비디오 파일 경로
	UPROPERTY(EditAnywhere, Category="Config|Media")
	FString FileDir{};

	// 5성 연출 비디오 식별자
	UPROPERTY(EditAnywhere, Category="Config|Media")
	FName Video_5Star{ TEXT("5Star") };

	// 인트로 연출 프리픽스 정의
	UPROPERTY(EditAnywhere, Category="Config|Media")
	FName IntroSpecial{ TEXT("GachaIntroSpecial") };

	UPROPERTY(EditAnywhere, Category="Config|Media")
	FName IntroNormal{ TEXT("GachaIntro") };

	UPROPERTY(EditAnywhere, Category="Config|Media")
	FName IntroPrefix{ TEXT("_Intro") };

	// ViewModel 캐시 (성능 최적화)
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UGachaViewModel>> CachedViewModels;

	// 가챠 결과 보상 목록
	UPROPERTY(Transient)
	TArray<FRewardHandler> GachaRewards;

protected:
	// 생명주기 관리
	virtual void Register() override;
	virtual void Unregister() override;

	/**
	 * 가챠 캠페인 데이터를 읽어 ViewModel 빌드
	 * DataTable을 순회하며 각 캠페인을 UI에 표시 가능한 형태로 변환
	 */
	UFUNCTION(BlueprintCallable)
	void BuildItems();

	/**
	 * Blueprint에서 구현: ViewModel 배열을 받아 UI 업데이트
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category="UI Update")
	void UpdateData(const TArray<UGachaViewModel*>& InData);

	// Enhanced Input System 통합
	virtual TArray<FWidgetInputHandler> GenerateInputs() const override;

	// 입력 이벤트 핸들러 (Blueprint 구현)
	UFUNCTION(BlueprintImplementableEvent, Category="Input Handler")
	void OnInputMove(const FInputActionValue& Value);

	UFUNCTION(BlueprintImplementableEvent, Category="Input Handler")
	void OnInputPickOne(const FInputActionValue& Value);

	UFUNCTION(BlueprintImplementableEvent, Category="Input Handler")
	void OnInputPickTen(const FInputActionValue& Value);

	UFUNCTION(BlueprintImplementableEvent, Category="Input Handler")
	void OnInputClose(const FInputActionValue& Value);

	/**
	 * ViewModel 조회 또는 생성
	 * 캐싱을 통해 불필요한 객체 생성 방지
	 */
	UFUNCTION(BlueprintPure)
	UGachaViewModel* FindOrAddViewModel(const FName& InRowName);

	/**
	 * 가챠 실행 (티켓 사용)
	 * @param InAmount 뽑기 횟수 (1 또는 10)
	 */
	UFUNCTION(BlueprintCallable)
	void OnExecutePickup(const int32 InAmount);

	/**
	 * 유료 재화로 티켓 구매 후 가챠 실행
	 * @param InAmount 구매할 티켓 수량
	 */
	UFUNCTION(BlueprintCallable)
	void OnExchangeTicket(const int32 InAmount);

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category="Event Handler")
	void OnUseTicket(const int32 InAmount);

	/**
	 * 가챠 결과 UI 표시
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category="Popup")
	void OpenGachaResult(const TArray<FRewardHandler>& InResults);

	/**
	 * 가챠 연출 비디오 설정
	 *
	 * 비디오 재생 순서:
	 * 1. Intro (일반 또는 고등급용)
	 * 2. 5성 연출 (해당되는 경우)
	 * 3. 캐릭터 인트로 (신규 캐릭터인 경우)
	 * 4. 결과 표시
	 *
	 * @param InHandlers 가챠 결과 배열
	 * @param bGradeHigh 고등급 연출 사용 여부
	 */
	UFUNCTION(BlueprintCallable)
	void SetVideosToPlay(const TArray<FRewardHandler>& InHandlers, const bool bGradeHigh = false);

	UFUNCTION(BlueprintImplementableEvent, Category="Video")
	void OnPlayVideo(const TArray<FVideoPlayHandler>& InHandlers);

	UFUNCTION(BlueprintImplementableEvent, Category="Video")
	void OnPlayVideos(const TArray<FVideoResourceData>& InData);

	UFUNCTION(BlueprintImplementableEvent, Category="Video")
	void OnVideoEnded();

private:
	// 비디오 재생 완료 콜백
	UFUNCTION()
	void OnEvent_VideoEnded();
};
