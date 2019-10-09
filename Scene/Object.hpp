#pragma once

#include <Core/CommandBuffer.hpp>
#include <Math/Geometry.hpp>
#include <Util/Util.hpp>

class Camera;
class Scene;

class Object {
public:
	const std::string mName;
	bool mEnabled;

	ENGINE_EXPORT Object(const std::string& name);
	ENGINE_EXPORT ~Object();

	inline Object* Parent() const { return mParent; }
	ENGINE_EXPORT bool Parent(Object* obj);
	ENGINE_EXPORT virtual bool AddChild(Object* obj);

	inline uint32_t ChildCount() const { return (uint32_t)mChildren.size(); }
	inline Object* Child(uint32_t index) const { return mChildren[index]; }

	inline float3 WorldPosition() { UpdateTransform(); return mWorldPosition; }
	inline quaternion WorldRotation() { UpdateTransform(); return mWorldRotation; }

	inline float3 LocalPosition() { UpdateTransform(); return mLocalPosition; }
	inline quaternion LocalRotation() { UpdateTransform(); return mLocalRotation; }
	inline float3 LocalScale() { UpdateTransform(); return mLocalScale; }

	inline float4x4 ObjectToWorld() { UpdateTransform(); return mObjectToWorld; }
	inline float4x4 WorldToObject() { UpdateTransform(); return mWorldToObject; }

	inline void LocalPosition(const float3& p) { mLocalPosition = p; Dirty(); }
	inline void LocalRotation(const quaternion& r) { mLocalRotation = r; Dirty(); }
	inline void LocalScale(const float3& s) { mLocalScale = s; Dirty(); }

	inline void LocalPosition(float x, float y, float z) { mLocalPosition.x = x; mLocalPosition.y = y; mLocalPosition.z = z; Dirty(); }
	inline void LocalScale(float x, float y, float z) { mLocalScale.x = x; mLocalScale.y = y; mLocalScale.z = z; Dirty(); }
	inline void LocalScale(float x) { mLocalScale.x = x; mLocalScale.y = x; mLocalScale.z = x; Dirty(); }

	ENGINE_EXPORT bool EnabledHeirarchy();
	ENGINE_EXPORT virtual AABB Bounds();
	ENGINE_EXPORT virtual AABB BoundsHeirarchy();

	ENGINE_EXPORT virtual void DrawGizmos(const FrameTime& frameTime, Camera* camera, CommandBuffer* commandBuffer, uint32_t backBufferIndex, Material* colorMaterial) {};


private:
	friend class Scene;
	Scene* mScene;

	bool mTransformDirty;
	float3 mLocalPosition;
	quaternion mLocalRotation;
	float3 mLocalScale;
	float4x4 mObjectToWorld;
	float4x4 mWorldToObject;

	float3 mWorldPosition;
	quaternion mWorldRotation;

	Object* mParent;
	std::vector<Object*> mChildren;

protected:
	ENGINE_EXPORT virtual void Dirty();
	ENGINE_EXPORT virtual bool UpdateTransform();
};