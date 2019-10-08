#include "audio.h"

#include <km_common/km_debug.h>

#include "main.h"

template <typename Allocator>
internal bool32 SoundInit(const ThreadContext* thread, Allocator* allocator,
	const GameAudio* audio, Sound* sound, const char* filePath)
{
	sound->play = false;
	sound->playing = false;
	sound->sampleIndex = 0;

	return LoadWAV(thread, allocator, filePath, audio, &sound->buffer);
}

internal void SoundUpdate(const GameAudio* audio, Sound* sound)
{
	if (sound->playing) {
		sound->sampleIndex += audio->sampleDelta;
		if (sound->sampleIndex >= sound->buffer.bufferSizeSamples) {
			sound->playing = false;
		}
	}
	if (sound->play) {
		sound->play = false;
		sound->playing = true;
		sound->sampleIndex = 0;
	}
}

internal void SoundWriteSamples(const Sound* sound, float32 amplitude,
	GameAudio* audio)
{
	if (!sound->playing) {
		return;
	}

	const AudioBuffer* buffer = &sound->buffer;
	uint64 samplesToWrite = audio->fillLength;
	if (sound->sampleIndex + samplesToWrite > buffer->bufferSizeSamples) {
		samplesToWrite = buffer->bufferSizeSamples - sound->sampleIndex;
	}
	for (uint64 i = 0; i < samplesToWrite; i++) {
		uint64 sampleInd = sound->sampleIndex + i;
		float32 sample1 = amplitude
			* buffer->buffer[sampleInd * audio->channels];
		float32 sample2 = amplitude
			* buffer->buffer[sampleInd * audio->channels + 1];

		audio->buffer[i * audio->channels] += sample1;
		audio->buffer[i * audio->channels + 1] += sample2;
	}
}

template <typename Allocator>
bool32 InitAudioState(const ThreadContext* thread, Allocator* allocator,
	AudioState* audioState, GameAudio* audio)
{
	// audioState->globalMute = false;
	audioState->globalMute = true;

	if (!SoundInit(thread, allocator, audio, &audioState->soundJump, "data/audio/yow.wav")) {
		LOG_ERROR("Failed to init jump sound");
		return false;
	}

#if GAME_INTERNAL
	audioState->debugView = false;
#endif

	return true;
}

void OutputAudio(GameAudio* audio, AppState* appState,
	const GameInput* input, MemoryBlock transient)
{
	DEBUG_ASSERT(audio->sampleDelta >= 0);
	DEBUG_ASSERT(audio->channels == 2); // Stereo support only
	AudioState* audioState = &appState->audioState;

	SoundUpdate(audio, &audioState->soundJump);

	for (int i = 0; i < audio->fillLength; i++) {
		audio->buffer[i * audio->channels] = 0.0f;
		audio->buffer[i * audio->channels + 1] = 0.0f;
	}

	if (audioState->globalMute) {
		return;
	}

	SoundWriteSamples(&audioState->soundJump, 1.0f, audio);
}

#if GAME_INTERNAL

internal void DrawAudioBuffer(
	const AppState* appState, const GameAudio* audio,
	const float32* buffer, uint64 bufferSizeSamples, uint8 channel,
	const int marks[], const Vec4 markColors[], int numMarks,
	Vec3 origin, Vec2 size, Vec4 color,
	MemoryBlock transient)
{
	DEBUG_ASSERT(transient.size >= sizeof(LineGLData));
	DEBUG_ASSERT(bufferSizeSamples <= MAX_LINE_POINTS);

	LineGLData* lineData = (LineGLData*)transient.memory;
	
	lineData->count = (int)bufferSizeSamples;
	for (int i = 0; i < bufferSizeSamples; i++) {
		float32 val = buffer[i * audio->channels + channel];
		float32 t = (float32)i / (bufferSizeSamples - 1);
		lineData->pos[i] = {
			origin.x + t * size.x,
			origin.y + size.y * val,
			origin.z
		};
	}
	DrawLine(appState->lineGL, Mat4::one, lineData, color);

	lineData->count = 2;
	for (int i = 0; i < numMarks; i++) {
		float32 tMark = (float32)marks[i] / (bufferSizeSamples - 1);
		lineData->pos[0] = Vec3 {
			origin.x + tMark * size.x,
			origin.y,
			origin.z
		};
		lineData->pos[1] = Vec3 {
			origin.x + tMark * size.x,
			origin.y + size.y,
			origin.z
		};
		DrawLine(appState->lineGL, Mat4::one, lineData, markColors[i]);
	}
}

void DrawDebugAudioInfo(const GameAudio* audio, AppState* appState,
	const GameInput* input, ScreenInfo screenInfo, MemoryBlock transient,
	Vec4 debugFontColor)
{
	AudioState* audioState = &appState->audioState;

	if (WasKeyPressed(input, KM_KEY_H)) {
		audioState->debugView = !audioState->debugView;
	}
}

#endif