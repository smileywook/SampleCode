/**
 * Video Player Subsystem
 *
 * 주요 기능:
 * - 멀티 비디오 시퀀스 재생 관리
 * - SRT 자막 파싱 및 동기화
 * - 오디오 포커스 제어 (배경음악 자동 조절)
 * - 스킵 및 루프 재생 지원
 *
 * 기술 스택:
 * - Unreal Engine 5 Media Framework
 * - FTickableGameObject (자막 동기화)
 * - GameInstanceSubsystem (싱글톤)
 * - Delegate 시스템
 */

#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "VideoPlayer.generated.h"

struct FVideoResourceData;
class UFileMediaSource;
class UMediaPlayer;
class UMediaSource;
class USoundClass;
class USoundMix;

// 델리게이트 선언
DECLARE_DYNAMIC_DELEGATE(FOnVideoPlaybackEnd);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSingleVideoEnd, int32, VideoIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllVideosEnd);

/**
 * 비디오 재생 옵션 데이터
 */
USTRUCT(BlueprintType)
struct FVideoOptionalData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	FName ResultName{ NAME_None };
};

/**
 * 비디오 재생 핸들러
 * 개별 비디오의 재생 설정을 담는 구조체
 */
USTRUCT(BlueprintType)
struct FVideoPlayHandler
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UMediaSource> MediaSource{ nullptr };

	UPROPERTY(BlueprintReadWrite)
	FOnVideoPlaybackEnd OnPlaybackEnd;

	UPROPERTY(BlueprintReadWrite)
	FString SubtitlePath;

	UPROPERTY(BlueprintReadWrite)
	bool bUseSkip{ false };

	UPROPERTY(BlueprintReadWrite)
	bool bForcePlay{ false };

	UPROPERTY(BlueprintReadWrite)
	bool bLoop{ false };

	UPROPERTY(BlueprintReadWrite)
	FName GroupID{ NAME_None };

	FVideoOptionalData VideoOptionalData{};
};

/**
 * 자막 큐 (SRT 포맷)
 */
USTRUCT(BlueprintType)
struct FSubtitleCue
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	FTimespan StartTime;

	UPROPERTY(BlueprintReadOnly)
	FTimespan EndTime;

	UPROPERTY(BlueprintReadOnly)
	FString SubtitleText;

	bool IsInTime(const FTimespan& Time) const;
};

/**
 * 자막 시스템
 *
 * 핵심 기능:
 * - SRT 파일 파싱
 * - 비디오 재생과 실시간 동기화
 * - FTickableGameObject를 통한 매 프레임 업데이트
 */
UCLASS()
class  USubtitle : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

	 USubtitle();
	explicit  USubtitle(FObjectInitializer& Initializer);

public:
	UPROPERTY()
	TArray<FSubtitleCue> SubtitleCue;

	/**
	 * SRT 파일 파싱
	 * @param FilePath 자막 파일 경로
	 */
	UFUNCTION(BlueprintCallable)
	void Parse(const FString& FilePath);

	/**
	 * 시간 문자열을 Timespan으로 변환
	 * Format: "HH:MM:SS,mmm"
	 */
	static FTimespan ParseTimeToTimespan(const FString& TimeString);

	void Play(UMediaPlayer* Player);
	void Stop();

	// FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override;
	virtual bool IsTickableWhenPaused() const override;
	virtual TStatId GetStatId() const override;

private:
	TWeakObjectPtr<UMediaPlayer> VideoPlayer;
	int32 CurrentIndex = 0;
	bool bIsShowingNarration = false;
};

/**
 * 비디오 플레이어 서브시스템
 *
 * 책임:
 * - 멀티 비디오 큐 관리
 * - 비디오 전환 로직
 * - 자막 동기화
 * - 오디오 포커스 제어
 * - 입력 블로킹 (비디오 재생 중)
 */
UCLASS(Abstract, Blueprintable)
class UVideoPlayer : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	static inline UVideoPlayer* Instance = nullptr;
	TUniquePtr<FPlayerBlockHandler> BlockHandler{ nullptr };

private:
	bool bOpenVideo{ false };

public:
	// 델리게이트
	UPROPERTY(BlueprintAssignable)
	FOnSingleVideoEnd OnSingleVideoEnd;

	UPROPERTY(BlueprintAssignable)
	FOnAllVideosEnd OnAllVideosEnd;

public:
	UPROPERTY(EditAnywhere)
	TObjectPtr<UMediaPlayer> MediaPlayer;

	UPROPERTY(EditAnywhere)
	TSubclassOf<UUserWidget> MediaWidgetClass;

	UPROPERTY(EditAnywhere)
	TMap<FName, UMediaSource*> MediaSources;

	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<AActor> MediaSoundProviderClass;

public: // Audio Focus
	UPROPERTY(EditDefaultsOnly, Category="Video|Audio Focus")
	TObjectPtr<USoundClass> UISoundClass{ nullptr };

	UPROPERTY(EditDefaultsOnly, Category="Video|Audio Focus")
	TObjectPtr<USoundMix> VideoFocusMix{ nullptr };

	UPROPERTY(EditDefaultsOnly, Category="Video|Audio Focus")
	float UIFocusTargetVolume{ 0.0f };

	UPROPERTY(EditDefaultsOnly, Category="Video|Audio Focus")
	float UIFocusFadeTime{ 0.2f };

private:
	bool bVideoAudioFocusActive = false;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MediaWidget;

	FVideoPlayHandler MediaHandler;

	TWeakObjectPtr<AActor> MediaSoundProvider;

	UPROPERTY(Transient)
	TObjectPtr<USubtitle> Subtitle;	

	// 비디오 큐
	UPROPERTY(Transient)
	TArray<FVideoPlayHandler> VideoQueue;

	// 미디어 소스 캐싱
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UFileMediaSource>> CachedMediaSource;

	int32 CurrentVideoIndex{ INDEX_NONE };
	int32 MaxVideoIndex{ INDEX_NONE };

	EUIName VideoPlayer{ EUIName::VideoPlayer };	

	float LastPlayStartTime{ 0.f };

public:
	static UVideoPlayer* Get() { return Instance; }

	/**
	 * 멀티 비디오 재생
	 * @param InVideoQueue 재생할 비디오 배열
	 * @param InVideoPlayer UI 식별자
	 */
	UFUNCTION(BlueprintCallable)
	static bool PlayVideos(const TArray<FVideoPlayHandler>& InVideoQueue, const EUIName InVideoPlayer = EUIName::VideoPlayer);

	/**
	 * 단일 비디오 재생
	 */
	UFUNCTION(BlueprintCallable)
	static bool PlayVideo(const FVideoPlayHandler& InVideoPlayHandler, const EUIName InVideoPlayer = EUIName::VideoPlayer);

	/**
	 * 리소스 데이터로부터 비디오 재생
	 * DataTable에서 정의된 비디오 설정 사용
	 */
	UFUNCTION(BlueprintCallable)
	static bool PlayVideosByResource(const TArray<FVideoResourceData>& InVideoQueue, const EUIName InVideoPlayer = EUIName::VideoPlayer);

	UFUNCTION(BlueprintCallable)
	static bool PlayVideoByResource(const FVideoResourceData& InResourceData, const EUIName InVideoPlayer = EUIName::VideoPlayer);

	// 재생 제어
	UFUNCTION(BlueprintCallable)
	static void PauseVideo();

	UFUNCTION(BlueprintCallable)
	static void ResumeVideo();

	UFUNCTION(BlueprintCallable)
	static void StopVideo();

	UFUNCTION(BlueprintCallable)
	static void NextVideo(const bool bSkipLoop = false);

	UFUNCTION(BlueprintCallable)
	static void SkipToNextVideo();

	UFUNCTION(BlueprintCallable)
	static void CloseVideo();

	/**
	 * 리소스 데이터를 재생 핸들러로 변환
	 */
	UFUNCTION(BlueprintCallable, meta=(ArrayParam="OutHandlers"))
	static void ConvertToHandlers(const FVideoResourceData& InResourceData, UPARAM(Ref) TArray<FVideoPlayHandler>& OutHandlers);

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	// 미디어 플레이어 이벤트 핸들러
	UFUNCTION()
	void OnMediaOpened(FString OpenedUrl);

	UFUNCTION()
	void OnMediaOpenFailed(FString FailedUrl);

	UFUNCTION()
	void OnMediaPlaybackEnd();

	void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld);
	virtual void OnFinishedAllVideos();

	/**
	 * 큐의 특정 인덱스 비디오 재생
	 */
	virtual bool PlayVideoAtIndex(const int32 Index);

	/**
	 * 다음 비디오 재생 (시퀀스 로직)
	 */
	void PlayNextInSequence();

	/**
	 * 미디어 소스 캐싱 및 조회
	 */
	TObjectPtr<UFileMediaSource> FindOrAddMediaSource(const FName& InGroupID, bool bIsLoop, const FString& InPath);

private: // Audio Focus
	/**
	 * 비디오 재생 시 배경 음악 볼륨 조절
	 * @param bEnable true: 배경음악 감소, false: 원래대로
	 */
	void ApplyVideoAudioFocus(bool bEnable);
};
