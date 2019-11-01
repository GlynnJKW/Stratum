#pragma once

#include <memory>
#include <variant>
#include <unordered_map>

#include <Content/Material.hpp>
#include <Content/Mesh.hpp>
#include <Core/DescriptorSet.hpp>
#include <Scene/Collider.hpp>
#include <Scene/Renderer.hpp>
#include <Util/Util.hpp>

class MeshRenderer : public Renderer, public Collider {
public:
	bool mVisible;

	ENGINE_EXPORT MeshRenderer(const std::string& name);
	ENGINE_EXPORT ~MeshRenderer();

	inline virtual void Mesh(::Mesh* m) { mMesh = m; Dirty(); }
	inline virtual void Mesh(std::shared_ptr<::Mesh> m) { mMesh = m; Dirty(); }
	inline virtual ::Mesh* Mesh() const { return mMesh.index() == 0 ? std::get<::Mesh*>(mMesh) : std::get<std::shared_ptr<::Mesh>>(mMesh).get(); }

	inline virtual std::shared_ptr<::Material> Material() const { return mMaterial; }
	ENGINE_EXPORT virtual void Material(std::shared_ptr<::Material> m);

	inline virtual bool Visible() override { return mVisible && Mesh() && EnabledHierarchy(); }
	inline virtual uint32_t RenderQueue() override { return mMaterial ? mMaterial->RenderQueue() : Renderer::RenderQueue(); }
	ENGINE_EXPORT virtual void Draw(const FrameTime& frameTime, Camera* camera, CommandBuffer* commandBuffer, uint32_t backBufferIndex, ::Material* materialOverride) override;
	ENGINE_EXPORT virtual void DrawGizmos(const FrameTime& frameTime, Camera* camera, CommandBuffer* commandBuffer, uint32_t backBufferIndex) override;

	inline virtual void CollisionMask(uint32_t m) { mCollisionMask = m; }
	inline virtual uint32_t CollisionMask() override { return mCollisionMask; }
	inline virtual OBB ColliderBounds() override { UpdateTransform(); return mOBB; }
	inline virtual AABB Bounds() override { UpdateTransform(); return mAABB; }

private:
	struct DeviceData {
		DescriptorSet** mDescriptorSets;
		Buffer** mBoundLightBuffers;
	};
	
	std::unordered_map<Device*, DeviceData> mDeviceData;
	uint32_t mCollisionMask;

protected:
	std::shared_ptr<::Material> mMaterial;
	uint8_t mNeedsObjectData;
	uint8_t mNeedsLightData;
	VkPushConstantRange mLightCountRange;

	OBB mOBB;
	AABB mAABB;
	std::variant<::Mesh*, std::shared_ptr<::Mesh>> mMesh;
	ENGINE_EXPORT virtual bool UpdateTransform() override;
};