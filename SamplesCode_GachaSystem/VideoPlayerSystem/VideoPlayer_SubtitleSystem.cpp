/**
 * Video Player - Subtitle System Implementation
 *
 * SRT 포맷 자막 파일 파싱 및 실시간 동기화 구현
 */

#include "VideoPlayer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Runtime/MediaAssets/Public/MediaPlayer.h"
#include "UI/Dialog/DialogUI.h"

bool FSubtitleCue::IsInTime(const FTimespan& Time) const
{
	return StartTime < Time && EndTime > Time;
}

#pragma region Subtitle System

USubtitle::USubtitle()
{}

USubtitle::USubtitle(FObjectInitializer& Initializer) : Super(Initializer)
{}

/**
 * SRT 파일 파싱
 *
 * SRT 포맷 구조:
 * 1
 * 00:00:01,000 --> 00:00:03,000
 * 첫 번째 자막 텍스트
 *
 * 2
 * 00:00:04,000 --> 00:00:06,000
 * 두 번째 자막 텍스트
 */
void USubtitle::Parse(const FString& InFilePath)
{
	SubtitleCue.Empty();

	const FString FullPath = FPaths::ProjectContentDir() + InFilePath;

	if (FString FileContent; FFileHelper::LoadFileToString(FileContent, *FullPath))
	{
		TArray<FString> Lines;
		FileContent.ParseIntoArrayLines(Lines);
		FSubtitleCue CurrentCue;

		for (int i = 0; i < Lines.Num(); i++)
		{
			// 인덱스 라인 (숫자만)
			if (Lines[i].IsNumeric())
			{
				CurrentCue = FSubtitleCue(); // 새로운 큐 시작
				continue;
			}

			// 타임라인 파싱 (HH:MM:SS,mmm --> HH:MM:SS,mmm)
			if (Lines[i].Contains("-->"))
			{
				TArray<FString> Times;
				Lines[i].ParseIntoArray(Times, TEXT("-->"), true);

				CurrentCue.StartTime = ParseTimeToTimespan(Times[0].TrimStartAndEnd());
				CurrentCue.EndTime = ParseTimeToTimespan(Times[1].TrimStartAndEnd());
			}
			// 자막 텍스트
			else if (!Lines[i].IsEmpty())
			{
				CurrentCue.SubtitleText = Lines[i];
				SubtitleCue.Add(CurrentCue);
			}
		}
	}
}

/**
 * SRT 시간 포맷을 Timespan으로 변환
 *
 * 입력 포맷: "00:01:23,456" (HH:MM:SS,mmm)
 * 출력: FTimespan 객체
 */
FTimespan USubtitle::ParseTimeToTimespan(const FString& InTimeString)
{
	TArray<FString> TimeParts;
	// 쉼표를 콜론으로 변환하여 파싱 간소화
	const FString CleanTimeString = InTimeString.Replace(TEXT(","), TEXT(":"));
	CleanTimeString.ParseIntoArray(TimeParts, TEXT(":"), true);

	if (TimeParts.Num() == 4) // HH:MM:SS:mmm
	{
		const int32 Hours = FCString::Atoi(*TimeParts[0]);
		const int32 Minutes = FCString::Atoi(*TimeParts[1]);
		const int32 Seconds = FCString::Atoi(*TimeParts[2]);
		const int32 Milliseconds = FCString::Atoi(*TimeParts[3]);

		return FTimespan(0, Hours, Minutes, Seconds, Milliseconds);
	}

	return FTimespan(0); // 파싱 실패 시 0 반환
}

void USubtitle::Play(UMediaPlayer* Player)
{
	VideoPlayer = Player;
	CurrentIndex = 0;
}

void USubtitle::Stop()
{
	if (bIsShowingNarration)
    {
        UDialogUI::CloseDialogWidget(GetFName());
    }

    bIsShowingNarration = false;
    CurrentIndex = 0;
    VideoPlayer = nullptr;
	SubtitleCue.Empty();
}

/**
 * 매 프레임 자막 동기화
 *
 * 로직:
 * 1. 현재 비디오 재생 시간 확인
 * 2. 현재 큐의 시작/종료 시간과 비교
 * 3. 자막 표시/숨김 처리
 */
void USubtitle::Tick(float DeltaTime)
{
	if (!VideoPlayer.IsValid())
	{
		return;
	}

	const FTimespan CurrentTime = VideoPlayer->GetTime();

	if (bIsShowingNarration)
	{
		// 현재 자막의 종료 시간 체크
		if (SubtitleCue[CurrentIndex].EndTime <= CurrentTime)
		{
			bIsShowingNarration = false;
			UDialogUI::CloseDialogWidget(GetFName());

			++CurrentIndex;

			// 모든 자막 표시 완료
			if (CurrentIndex >= SubtitleCue.Num())
			{
				VideoPlayer = nullptr;
				CurrentIndex = 0;
			}
		}
	}
	else
	{
		// 다음 자막의 시작 시간 체크
		if (SubtitleCue[CurrentIndex].StartTime <= CurrentTime)
		{
			bIsShowingNarration = true;
			UDialogUI* DialogWidget{ UDialogUI::GetDialogWidget() };

			if (DialogWidget == nullptr)
			{
				return;
			}

			const FText SubtitleText = FText(FText::GetCommon((SubtitleCue[CurrentIndex].SubtitleText)));
			DialogWidget->ShowNarration(GetFName(), SubtitleText, false);
		}
	}
}

/**
 * Tick 활성화 조건
 * - VideoPlayer가 유효하고
 * - 아직 표시할 자막이 남아있을 때
 */
bool USubtitle::IsTickable() const
{
	return VideoPlayer.IsValid() && CurrentIndex < SubtitleCue.Num();
}

bool USubtitle::IsTickableInEditor() const
{
	return false;
}

bool USubtitle::IsTickableWhenPaused() const
{
	return false;
}

TStatId USubtitle::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USubtitle, STATGROUP_Tickables);
}

#pragma endregion Subtitle System
