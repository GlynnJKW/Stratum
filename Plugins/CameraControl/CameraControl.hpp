#pragma once

#include <Core/EnginePlugin.hpp>
#include <Scene/TextRenderer.hpp>
#include <Input/MouseKeyboardInput.hpp>

class CameraControl : public EnginePlugin {
public:
	PLUGIN_EXPORT CameraControl();
	PLUGIN_EXPORT ~CameraControl();

	PLUGIN_EXPORT bool Init(Scene* scene) override;
	PLUGIN_EXPORT void Update(const FrameTime& frameTime) override;
	PLUGIN_EXPORT void PostRender(const FrameTime& frameTime, Camera* camera, CommandBuffer* commandBuffer, uint32_t backBufferIndex);

	inline void CameraDistance(float d) { mCameraDistance = d; }
	inline float CameraDistance() const { return mCameraDistance; }
	inline float3 CameraEuler() const { return mCameraEuler; }
	inline void CameraEuler(const float3& e) { mCameraEuler = e; }
	inline Object* CameraPivot() const { return mCameraPivot; }

private:
	Scene* mScene;
	float mCameraDistance;
	float3 mCameraEuler;

	MouseKeyboardInput* mInput;

	TextRenderer* mFpsText;
	Object* mCameraPivot;

	float mFrameTimeAccum;
	float mFps;
	uint32_t mFrameCount;
	uint32_t mTriangleCount;
};