#include "main.h"

#include <clang-c/Index.h>
#include <km_common/km_debug.h>
#include <km_common/km_defines.h>
#include <km_common/km_input.h>
#include <km_common/km_log.h>
#include <km_common/km_math.h>
#include <km_common/km_memory.h>
#include <km_common/km_string.h>
#include <km_platform/main_platform.h>
#undef internal
#include <random>
#define internal static
#undef STB_SPRINTF_IMPLEMENTATION
#include <stb_sprintf.h>

#include <vector>

#include "imgui.h"
#include "opengl.h"
#include "opengl_funcs.h"
#include "opengl_base.h"
#include "post.h"

struct ASTNode
{
	std::string usr;
	std::vector<ASTNode> children;
};

struct AST
{
	ASTNode root;

	void AddNode(std::string name, std::string parent)
	{
	}
};

void GameUpdateAndRender(const ThreadContext* thread, const PlatformFunctions* platformFuncs,
	const GameInput* input, ScreenInfo screenInfo, float32 deltaTime,
	GameMemory* memory, GameAudio* audio)
{
	// NOTE: for clarity
	// A call to this function means the following has happened, in order:
	//  1. A frame has been displayed to the user
	//  2. The latest user input has been processed by the platform layer
	//
	// This function is expected to update the state of the game
	// and draw the frame that will be displayed, ideally, some constant
	// amount of time in the future.
	DEBUG_ASSERT(sizeof(AppState) <= memory->permanent.size);
	AppState *appState = (AppState*)memory->permanent.memory;

	// NOTE make sure deltaTime values are reasonable
	const float32 MAX_DELTA_TIME = 1.0f / 10.0f;
	if (deltaTime < 0.0f) {
		LOG_ERROR("Negative deltaTime %f !!\n", deltaTime);
		deltaTime = 1.0f / 60.0f; // eh... idk
	}
	if (deltaTime > MAX_DELTA_TIME) {
		LOG_WARN("Large deltaTime %f, capped to %f\n", deltaTime, MAX_DELTA_TIME);
		deltaTime = MAX_DELTA_TIME;
	}

	if (memory->shouldInitGlobalVariables) {
		// Initialize global function names
		#define FUNC(returntype, name, ...) name = \
		platformFuncs->glFunctions.name;
			GL_FUNCTIONS_BASE
			GL_FUNCTIONS_ALL
		#undef FUNC

		memory->shouldInitGlobalVariables = false;
		LOG_INFO("Initialized global variables\n");
	}

	if (!memory->isInitialized) {
		LinearAllocator allocator(memory->transient.size, memory->transient.memory);

		// Very explicit depth testing setup (DEFAULT VALUES)
		// NDC is left-handed with this setup
		// (subtle left-handedness definition:
		//  front objects have z = -1, far objects have z = 1)
		// Nearer objects have less z than farther objects
		glDepthFunc(GL_LEQUAL);
		// Depth buffer clears to farthest z-value (1)
		glClearDepth(1.0);
		// Depth buffer transforms -1 to 1 range to 0 to 1 range
		glDepthRange(0.0, 1.0);

		glDisable(GL_CULL_FACE);
		//glFrontFace(GL_CCW);
		//glCullFace(GL_BACK);

		glLineWidth(1.0f);

		if (!InitAudioState(thread, &allocator, &appState->audioState, audio)) {
			DEBUG_PANIC("Failed to init audio state\n");
		}

		appState->rectGL = InitRectGL(thread, &allocator);
		appState->texturedRectGL = InitTexturedRectGL(thread, &allocator);
		appState->lineGL = InitLineGL(thread, &allocator);
		appState->textGL = InitTextGL(thread, &allocator);

		FT_Error error = FT_Init_FreeType(&appState->ftLibrary);
		if (error) {
			LOG_ERROR("FreeType init error: %d\n", error);
		}
		appState->fontFaceSmall = LoadFontFace(thread, &allocator, appState->ftLibrary,
			"data/fonts/ocr-a/regular.ttf", 18);
		appState->fontFaceMedium = LoadFontFace(thread, &allocator, appState->ftLibrary,
			"data/fonts/ocr-a/regular.ttf", 24);

		InitializeFramebuffers(NUM_FRAMEBUFFERS_COLOR_DEPTH, appState->framebuffersColorDepth);
		InitializeFramebuffers(NUM_FRAMEBUFFERS_COLOR, appState->framebuffersColor);
		InitializeFramebuffers(NUM_FRAMEBUFFERS_GRAY, appState->framebuffersGray);

		glGenVertexArrays(1, &appState->screenQuadVertexArray);
		glBindVertexArray(appState->screenQuadVertexArray);

		glGenBuffers(1, &appState->screenQuadVertexBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, appState->screenQuadVertexBuffer);
		const GLfloat vertices[] = {
			-1.0f, -1.0f,
			1.0f, -1.0f,
			1.0f, 1.0f,
			1.0f, 1.0f,
			-1.0f, 1.0f,
			-1.0f, -1.0f
		};
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
			GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			0, // match shader layout location
			2, // size (vec2)
			GL_FLOAT, // type
			GL_FALSE, // normalized?
			0, // stride
			(void*)0 // array buffer offset
		);

		glGenBuffers(1, &appState->screenQuadUVBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, appState->screenQuadUVBuffer);
		const GLfloat uvs[] = {
			0.0f, 0.0f,
			1.0f, 0.0f,
			1.0f, 1.0f,
			1.0f, 1.0f,
			0.0f, 1.0f,
			0.0f, 0.0f
		};
		glBufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_STATIC_DRAW);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(
			1, // match shader layout location
			2, // size (vec2)
			GL_FLOAT, // type
			GL_FALSE, // normalized?
			0, // stride
			(void*)0 // array buffer offset
		);

		glBindVertexArray(0);

		appState->screenShader = LoadShaders(thread, &allocator,
			"shaders/screen.vert", "shaders/screen.frag");
		appState->bloomExtractShader = LoadShaders(thread, &allocator,
			"shaders/screen.vert", "shaders/bloomExtract.frag");
		appState->bloomBlendShader = LoadShaders(thread, &allocator,
			"shaders/screen.vert", "shaders/bloomBlend.frag");
		appState->blurShader = LoadShaders(thread, &allocator,
			"shaders/screen.vert", "shaders/blur.frag");
		appState->grainShader = LoadShaders(thread, &allocator,
			"shaders/screen.vert", "shaders/grain.frag");
		appState->lutShader = LoadShaders(thread, &allocator,
			"shaders/screen.vert", "shaders/lut.frag");

		CXIndex index = clang_createIndex(0, 0);
		CXTranslationUnit unit = clang_parseTranslationUnit(
			index, "data/other/test.cpp", nullptr, 0,
			nullptr, 0,
			CXTranslationUnit_None
		);
		if (unit == nullptr) {
			DEBUG_PANIC("Failed to parse translation unit");
		}

		AST ast;
		CXCursor cursor = clang_getTranslationUnitCursor(unit);
		clang_visitChildren(
			cursor,
			[](CXCursor c, CXCursor parent, CXClientData client_data)
			{
				CXString cursorSpelling = clang_getCursorSpelling(c);
				CXString cursorKindSpelling = clang_getCursorKindSpelling(clang_getCursorKind(c));
				LOG_INFO("Cursor %s\n", clang_getCString(cursorSpelling));
				LOG_INFO("    type %s\n", clang_getCString(cursorKindSpelling));
				LOG_FLUSH();
				clang_disposeString(cursorSpelling);
				clang_disposeString(cursorKindSpelling);
				return CXChildVisit_Recurse;
			},
			nullptr
		);

		clang_disposeTranslationUnit(unit);
		clang_disposeIndex(index);

		memory->isInitialized = true;
	}
	if (screenInfo.changed) {
		// TODO not ideal to check for changed screen every frame
		// probably not that big of a deal, but might also be easy to avoid
		// later on with a more callback-y mechanism?

		UpdateFramebufferColorAttachments(NUM_FRAMEBUFFERS_COLOR_DEPTH,
			appState->framebuffersColorDepth,
			GL_RGB, screenInfo.size.x, screenInfo.size.y, GL_RGB, GL_UNSIGNED_BYTE);
		UpdateFramebufferColorAttachments(NUM_FRAMEBUFFERS_COLOR, appState->framebuffersColor,
			GL_RGB, screenInfo.size.x, screenInfo.size.y, GL_RGB, GL_UNSIGNED_BYTE);
		UpdateFramebufferColorAttachments(NUM_FRAMEBUFFERS_GRAY, appState->framebuffersGray,
			GL_RED, screenInfo.size.x, screenInfo.size.y, GL_RED, GL_FLOAT);

		UpdateFramebufferDepthAttachments(NUM_FRAMEBUFFERS_COLOR_DEPTH,
			appState->framebuffersColorDepth,
			GL_DEPTH24_STENCIL8, screenInfo.size.x, screenInfo.size.y,
			GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8);

		LOG_INFO("Updated screen-size-dependent info\n");
	}

	// ---------------------------- Begin Rendering ---------------------------
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindFramebuffer(GL_FRAMEBUFFER, appState->framebuffersColorDepth[0].framebuffer);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	DrawRect(appState->rectGL, screenInfo,
		Vec2Int { 100, 100 }, Vec2::zero, Vec2Int { 100, 100 },
		Vec4 { 1.0f, 1.0f, 1.0f, 1.0f });

	{
		Panel testPanel;
		testPanel.Begin();

		testPanel.Text("Hello, sailor");

		testPanel.End();
	}

	// ------------------------ Post processing passes ------------------------
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	// Apply filters
	// PostProcessGrain(appState->framebuffersColorDepth[0], appState->framebuffersColor[0],
	//     appState->screenQuadVertexArray,
	//     appState->grainShader, appState->grainTime);

	// PostProcessLUT(appState->framebuffersColorDepth[0],
	// 	appState->framebuffersColor[0],
	// 	appState->screenQuadVertexArray,
	// 	appState->lutShader, appState->lutBase);

	// --------------------------- Render to screen ---------------------------
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glBindVertexArray(appState->screenQuadVertexArray);
	glUseProgram(appState->screenShader);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, appState->framebuffersColorDepth[0].color);
	GLint loc = glGetUniformLocation(appState->screenShader, "framebufferTexture");
	glUniform1i(loc, 0);

	glDrawArrays(GL_TRIANGLES, 0, 6);

	// ---------------------------- End Rendering -----------------------------

	OutputAudio(audio, appState, input, memory->transient);

#if GAME_SLOW
	// Catch-all site for OpenGL errors
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR) {
		LOG_ERROR("OpenGL error: 0x%x\n", err);
	}
#endif
}

#include "audio.cpp"
#include "framebuffer.cpp"
#include "imgui.cpp"
#include "load_png.cpp"
#include "load_psd.cpp"
#include "load_wav.cpp"
#include "opengl_base.cpp"
#include "post.cpp"
#include "text.cpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
#define STB_SPRINTF_IMPLEMENTATION
#include <stb_sprintf.h>

#include <km_common/km_debug.cpp>
#include <km_common/km_input.cpp>
#include <km_common/km_lib.cpp>
#include <km_common/km_log.cpp>
#include <km_common/km_memory.cpp>
#include <km_common/km_string.cpp>

#if GAME_WIN32
#include <km_platform/win32_main.cpp>
#include <km_platform/win32_audio.cpp>
// TODO else other platforms...
#endif