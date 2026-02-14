/**
 * Video Player - Core System Implementation
 *
 * 핵심 기능:
 * 1. 멀티 비디오 큐 관리
 * 2. 자동 시퀀스 재생
 * 3. 오디오 포커스 제어
 */

#include "VideoPlayer.h"
#include "Blueprint/UserWidget.h"
#include "DataTable/VideoResourceData.h"
#include "FileMediaSource.h"
#include "MediaPlayer.h"
#include "Kismet/GameplayStatics.h"

void UVideoPlayer::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Instance = this;

	// 미디어 플레이어 이벤트 바인딩
	MediaPlayer->OnEndReached.AddUniqueDynamic(this, &UVideoPlayer::OnMediaPlaybackEnd);
	MediaPlayer->OnMediaOpened.AddUniqueDynamic(this, &UVideoPlayer::OnMediaOpened);
	MediaPlayer->OnMediaOpenFailed.AddUniqueDynamic(this, &UVideoPlayer::OnMediaOpenFailed);

	MediaPlayer->SetLooping(false);
	MediaPlayer->PlayOnOpen = false;

	Subtitle = NewObject<USubtitle>();

	if (UGameInstance* GameInstance = UGameInstance::Get())
	{
		GameInstance->OnWorldChangedDelegate.AddUObject(this, &ThisClass::OnWorldChanged);
	}
}

void UVideoPlayer::Deinitialize()
{
	Super::Deinitialize();
	Instance = nullptr;
}

/**
 * 멀티 비디오 재생
 *
 * 프로세스:
 * 1. 비디오 큐 초기화
 * 2. 첫 번째 비디오부터 재생 시작
 * 3. OnMediaPlaybackEnd에서 자동으로 다음 비디오 재생
 */
bool UVideoPlayer::PlayVideos(const TArray<FVideoPlayHandler>& InVideoQueue, const EUIName InVideoPlayer)
{
	Instance->VideoQueue.Reset();
	if (InVideoQueue.Num() == 0)
	{
		return false;
	}

	Instance->VideoPlayer = InVideoPlayer;
	Instance->VideoQueue = InVideoQueue;
	Instance->CurrentVideoIndex = 0;
	Instance->MaxVideoIndex = InVideoQueue.Num() - 1;

	return Instance->PlayVideoAtIndex(0);
}

bool UVideoPlayer::PlayVideoAtIndex(const int32 Index)
{
	if (!VideoQueue.IsValidIndex(Index))
	{
		return false;
	}

	return PlayVideo(VideoQueue[Index], Instance->VideoPlayer);
}

/**
 * 단일 비디오 재생
 *
 * 주요 작업:
 * - UI 오픈
 * - 자막 로드
 * - 플레이어 입력 블로킹
 * - 오디오 포커스 활성화
 */
bool UVideoPlayer::PlayVideo(const FVideoPlayHandler& InVideoPlayHandler, const EUIName InVideoPlayer)
{
	if (!IsValid(InVideoPlayHandler.MediaSource))
	{
		ensure(false);
		return false;
	}

	if (Instance->MediaPlayer->IsPlaying())
	{
		return false;
	}

	Instance->VideoPlayer = InVideoPlayer;
	Instance->MediaHandler = InVideoPlayHandler;

	// UI 오픈
	UUIBlueprintLibrary::OpenUIByName(Instance->VideoPlayer);
	if (ULoadingUI* VideoPlayerUI = Cast<ULoadingUI>(UUIManager::GetUIScreen(Instance->VideoPlayer)))
	{
		VideoPlayerUI->SetOptionalData(InVideoPlayHandler.VideoOptionalData, InVideoPlayHandler.bLoop);
		VideoPlayerUI->SetUseSkip(InVideoPlayHandler.bUseSkip);
	}

	// 로그 : [VideoPlayer] PlayVideo MediaSource[%s], Loop=%d"

	// 미디어 소스 열기
	const bool bSucceed = Instance->MediaPlayer->OpenSource(Instance->MediaHandler.MediaSource);
	Instance->bOpenVideo = true;
	Instance->MediaPlayer->SetLooping(InVideoPlayHandler.bLoop);

	if (!bSucceed)
	{
		// 로그 : [VideoPlayer] OpenSource Failed [%s]
		Instance->bOpenVideo = false;
		UUIBlueprintLibrary::CloseUIByName(Instance->VideoPlayer);
		return false;
	}

	Instance->LastPlayStartTime = FPlatformTime::Seconds();

	// 자막 파싱 및 활성화
	if (!VideoPlayHandler.SubtitlePath.IsEmpty())
	{
		Instance->Subtitle->Parse(VideoPlayHandler.SubtitlePath);
		Instance->Subtitle->Play(Instance->MediaPlayer);
		// 로그 : [VideoPlayer] SubtitlePath = [%s]
	}
	else
	{
		Instance->Subtitle->Stop();
	}

	// 플레이어 입력 블로킹 (비디오 재생 중 조작 방지)
	if (APlayerController* Controller = Cast<APlayerController>(Instance->GetWorld()->GetFirstPlayerController()))
	{
		Instance->BlockHandler = MakeUnique<FPlayerBlockHandler>(Instance->GetFName(), EControllerBlockMask::BlockAll);
		Controller->ApplyControlBlock(Instance->BlockHandler.Get());
	}

	UBlueprintLibrary::SetUsingIdleAnimation(Instance->GetWorld(), true);

	return bSucceed;
}

/**
 * 리소스 데이터를 재생 핸들러로 변환
 *
 * 하나의 비디오 리소스는:
 * - Prologue (인트로)
 * - Loop (루프 영상)
 * 두 개의 핸들러로 구성될 수 있음
 */
void UVideoPlayer::ConvertToHandlers(const FVideoResourceData& InResourceData, TArray<FVideoPlayHandler>& OutHandlers)
{
	// Prologue 영상
	if (!InResourceData.ProloguePath.IsEmpty())
	{
		FVideoPlayHandler PrologueHandler;
		UFileMediaSource* FileSource = Instance->FindOrAddMediaSource(InResourceData.GroupID, false, InResourceData.RootPath + InResourceData.ProloguePath);

		PrologueHandler.MediaSource = FileSource;
		PrologueHandler.SubtitlePath = InResourceData.PrologueSubtitle;
		PrologueHandler.bUseSkip = InResourceData.bUseSkip;
		PrologueHandler.bForcePlay = InResourceData.bForcePlay;
		PrologueHandler.bLoop = false;
		PrologueHandler.GroupID = InResourceData.GroupID;
		PrologueHandler.VideoOptionalData.ResultName = InResourceData.VideoName;
		OutHandlers.Add(PrologueHandler);
	}

	// Loop 영상
	if (!InResourceData.LoopPath.IsEmpty())
	{
		FVideoPlayHandler LoopHandler;
		UFileMediaSource* FileSource = Instance->FindOrAddMediaSource(InResourceData.GroupID, true, InResourceData.RootPath + InResourceData.LoopPath);

		LoopHandler.MediaSource = FileSource;
		LoopHandler.SubtitlePath = InResourceData.LoopSubtitle;
		LoopHandler.bUseSkip = InResourceData.bUseSkip;
		LoopHandler.bForcePlay = false;
		LoopHandler.bLoop = true;
		LoopHandler.GroupID = InResourceData.GroupID;
		LoopHandler.VideoOptionalData.ResultName = InResourceData.VideoName;
		OutHandlers.Add(LoopHandler);
	}
}

void UVideoPlayer::OnMediaOpened(FString OpenedUrl)
{
	// 로그 : [VideoPlayer] OnMediaOpened URL[%s]"

	const FTimespan Duration = Instance->MediaPlayer->GetDuration();
	// 로그 : [VideoPlayer] Media Duration = %f sec

	if (!Instance->MediaPlayer->IsPlaying())
	{
		if (!Instance->MediaPlayer->Play())
		{
			// 로그 : [VideoPlayer] OnMediaOpened - Play() Failed for [%s]
			OnFinishedAllVideos();
			return;
		}
	}

	// 오디오 포커스 활성화 (배경음악 볼륨 감소)
	Instance->ApplyVideoAudioFocus(true);

	if (ULoadingUI* VideoPlayerUI = Cast<ULoadingUI>(UUIManager::GetUIScreen(Instance->VideoPlayer)))
    {
		Instance->bOpenVideo = false;
        VideoPlayerUI->OnMediaOpened();
    }
}

void UVideoPlayer::OnMediaOpenFailed(FString FailedUrl)
{
	// 로그 : [VideoPlayer] OnMediaOpenFailed URL[%s]

	ApplyVideoAudioFocus(false);
	UUIBlueprintLibrary::CloseUIByName(Instance->VideoPlayer);
}

/**
 * 비디오 재생 완료 이벤트
 *
 * 처리 로직:
 * 1. False End 이벤트 필터링 (너무 빠른 종료 무시)
 * 2. 루프 비디오는 계속 재생
 * 3. 일반 비디오는 다음 비디오로 전환
 */
void UVideoPlayer::OnMediaPlaybackEnd()
{
	if (bOpenVideo)
	{
		return;
	}

	if (Subtitle)
	{
		Subtitle->Stop();
	}

    const FTimespan Duration = MediaPlayer->GetDuration();
    const FTimespan Current  = MediaPlayer->GetTime();

	// False End 이벤트 필터링
    const double Elapsed = FPlatformTime::Seconds() - LastPlayStartTime;
    if (Elapsed < 0.5f)
    {
        // 로그 : [VideoPlayer] Ignored false End (Elapsed=%.3f, Current=%.3f, Duration=%.3f)
        return;
    }

    if (Duration.GetTotalSeconds() > 1.0 && Current.GetTotalSeconds() < Duration.GetTotalSeconds() * 0.95)
    {
        // 로그 : [VideoPlayer] Ignored false End event. Current=%f / Duration=%f
    }

    // 로그 : [VideoPlayer] OnMediaPlaybackEnd

    if (!VideoQueue.IsValidIndex(CurrentVideoIndex))
    {
        OnFinishedAllVideos();
        return;
    }

    const FVideoPlayHandler& CurrentHandler = VideoQueue[CurrentVideoIndex];

	// 루프 비디오는 자동으로 계속 재생
    if (CurrentHandler.bLoop)
    {
        // 로그 : [VideoPlayer] Loop video reached end once, continuing loop playback.
        return;
    }

    OnSingleVideoEnd.Broadcast(CurrentVideoIndex);
    PlayNextInSequence();
}

/**
 * 다음 비디오 재생 시퀀스
 *
 * 로직:
 * - 현재 비디오가 Prologue라면 같은 그룹의 Loop 비디오 찾기
 * - 그렇지 않으면 다음 비루프 비디오 찾기
 * - 더 이상 비디오가 없으면 종료
 */
void UVideoPlayer::PlayNextInSequence()
{
	// 로그 : [VideoPlayer] PlayNextInSequence

	if (!VideoQueue.IsValidIndex(CurrentVideoIndex))
    {
        OnFinishedAllVideos();
        return;
    }

    const FVideoPlayHandler& CurrentHandler = VideoQueue[CurrentVideoIndex];

	// 현재 비디오가 Prologue인 경우, 같은 그룹의 Loop 비디오 찾기
    if (!CurrentHandler.bLoop)
    {
        const int32 NextIndex = CurrentVideoIndex + 1;
        if (VideoQueue.IsValidIndex(NextIndex))
        {
            const FVideoPlayHandler& NextHandler = VideoQueue[NextIndex];
            if (NextHandler.GroupID == CurrentHandler.GroupID && NextHandler.bLoop)
            {
                CurrentVideoIndex = NextIndex;
                PlayVideoAtIndex(CurrentVideoIndex);
                return;
            }
        }
    }

	// 다음 비루프 비디오 찾기
    for (int32 i = CurrentVideoIndex + 1; i < VideoQueue.Num(); ++i)
    {
        const FVideoPlayHandler& NextHandler = VideoQueue[i];
        if (!NextHandler.bLoop)
        {
            CurrentVideoIndex = i;
            PlayVideoAtIndex(CurrentVideoIndex);
            return;
        }
    }

    OnFinishedAllVideos();
}

/**
 * 모든 비디오 재생 완료 처리
 *
 * 정리 작업:
 * - 자막 종료
 * - 오디오 포커스 해제
 * - UI 닫기
 * - 입력 블록 해제
 */
void UVideoPlayer::OnFinishedAllVideos()
{
	// 로그 : [VideoPlayer] OnFinishedAllVideos

	if (Subtitle)
	{
		Subtitle->Stop();
	}

	ApplyVideoAudioFocus(false);

	MediaPlayer->Close();
	VideoQueue.Reset();

	OnAllVideosEnd.Broadcast();
	UUIBlueprintLibrary::CloseUIByName(Instance->VideoPlayer);

	if (BlockHandler)
	{
		BlockHandler.Reset();
	}

	UBlueprintLibrary::SetUsingIdleAnimation(GetWorld(), false);

	if (MediaHandler.OnPlaybackEnd.IsBound())
	{
		MediaHandler.OnPlaybackEnd.Execute();
		MediaHandler.OnPlaybackEnd.Clear();
	}
}

/**
 * 미디어 소스 캐싱 시스템
 *
 * 이점:
 * - 동일한 비디오 재생 시 파일 재로드 방지
 * - 메모리 효율성
 */
TObjectPtr<UFileMediaSource> UVideoPlayer::FindOrAddMediaSource(const FName& GroupID, bool bIsLoop, const FString& Path)
{
	const FString KeyString = FString::Printf(TEXT("%s_%s"), *GroupID.ToString(), bIsLoop ? TEXT("Loop") : TEXT("Prologue"));
	const FName KeyName(*KeyString);

	TObjectPtr<UFileMediaSource>& MediaSource = CachedMediaSource.FindOrAdd(KeyName);
	if (!MediaSource)
	{
		MediaSource = NewObject<UFileMediaSource>(this);
		MediaSource->SetFilePath(Path);
	}
	return MediaSource;
}

/**
 * 오디오 포커스 제어
 *
 * 비디오 재생 시 배경 음악을 자동으로 줄여
 * 비디오 오디오에 집중할 수 있도록 함
 */
void UVideoPlayer::ApplyVideoAudioFocus(bool bEnable)
{
	if (!GetWorld() || !UISoundClass || !VideoFocusMix)
	{
		bVideoAudioFocusActive = false;
		return;
	}

	if (bEnable)
	{
		if (bVideoAudioFocusActive)
		{
			return;
		}

		UGameplayStatics::SetSoundMixClassOverride(GetWorld(), VideoFocusMix, UISoundClass,	UIFocusTargetVolume, 1.0f, UIFocusFadeTime, true);
		UGameplayStatics::PushSoundMixModifier(GetWorld(), VideoFocusMix);
		bVideoAudioFocusActive = true;
	}
	else
	{
		if (!bVideoAudioFocusActive)
		{
			return;
		}

		UGameplayStatics::ClearSoundMixClassOverride(GetWorld(), VideoFocusMix, UISoundClass, UIFocusFadeTime);
		UGameplayStatics::PopSoundMixModifier(GetWorld(), VideoFocusMix);
		bVideoAudioFocusActive = false;
	}
}
