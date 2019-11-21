#include <Scene/Scene.hpp>
#include <Scene/Renderer.hpp>
#include <Scene/MeshRenderer.hpp>
#include <Core/Instance.hpp>
#include <Util/Profiler.hpp>

using namespace std;

#define INSTANCE_BATCH_SIZE 4096
#define MAX_GPU_LIGHTS 64
#define SHADOW_ATLAS_RESOLUTION 2048
#define SHADOW_RESOLUTION 512

Scene::Scene(::Instance* instance, ::AssetManager* assetManager, ::InputManager* inputManager, ::PluginManager* pluginManager)
	: mInstance(instance), mAssetManager(assetManager), mInputManager(inputManager), mPluginManager(pluginManager), mDrawGizmos(false) {
	mGizmos = new ::Gizmos(this);
	mShadowTexelSize = float2(1.f / SHADOW_ATLAS_RESOLUTION, 1.f / SHADOW_ATLAS_RESOLUTION) * .75f;
	mCascadeSplits = float4(.005f, .1f, .25f, 1.f);
}
Scene::~Scene(){
	for (auto& kp : mDeviceData) {
		for (uint32_t i = 0; i < kp.first->MaxFramesInFlight(); i++) {
			safe_delete(kp.second.mLightBuffers[i]);
			safe_delete(kp.second.mShadowBuffers[i]);
		}
		safe_delete_array(kp.second.mLightBuffers);
		safe_delete_array(kp.second.mShadowBuffers);
		safe_delete(kp.second.mShadowAtlasFramebuffer);
		for (Camera* c : kp.second.mShadowCameras) safe_delete(c);
	}
	safe_delete(mGizmos);

	while (mObjects.size())
		RemoveObject(mObjects[0].get());

	mCameras.clear();
	mRenderers.clear();
	mLights.clear();
	mObjects.clear();
}

void Scene::Update() {
	PROFILER_BEGIN("Pre Update");
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled)
			p->PreUpdate();
	PROFILER_END;

	PROFILER_BEGIN("Update");
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled)
			p->Update();
	PROFILER_END;

	PROFILER_BEGIN("Post Update");
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled)
			p->PostUpdate();
	PROFILER_END;
}

void Scene::AddObject(shared_ptr<Object> object) {
	mObjects.push_back(object);
	object->mScene = this;

	if (auto l = dynamic_cast<Light*>(object.get()))
		mLights.push_back(l);
	if (auto c = dynamic_cast<Camera*>(object.get()))
		mCameras.push_back(c);
	if (auto r = dynamic_cast<Renderer*>(object.get()))
		mRenderers.push_back(r);
}
void Scene::RemoveObject(Object* object) {
	if (!object) return;

	if (auto l = dynamic_cast<Light*>(object))
		for (auto it = mLights.begin(); it != mLights.end();) {
			if (*it == l) {
				it = mLights.erase(it);
				break;
			} else
				it++;
		}

	if (auto c = dynamic_cast<Camera*>(object))
		for (auto it = mCameras.begin(); it != mCameras.end();) {
			if (*it == c) {
				it = mCameras.erase(it);
				break;
			} else
				it++;
		}

	if (auto r = dynamic_cast<Renderer*>(object))
		for (auto it = mRenderers.begin(); it != mRenderers.end();) {
			if (*it == r) {
				it = mRenderers.erase(it);
				break;
			} else
				it++;
		}

	for (auto it = mObjects.begin(); it != mObjects.end();)
		if (it->get() == object) {
			while (object->mChildren.size())
				object->RemoveChild(object->mChildren[0]);
			if (object->mParent) object->mParent->RemoveChild(object);
			object->mParent = nullptr;
			object->mScene = nullptr;
			it = mObjects.erase(it);
			break;
		} else
			it++;
}

void Scene::AddShadowCamera(DeviceData* dd, uint32_t si, ShadowData* sd, bool ortho, float size, const float3& pos, const quaternion& rot, float near, float far) {
	if (dd->mShadowCameras.size() <= si)
		dd->mShadowCameras.push_back(new Camera("ShadowCamera", dd->mShadowAtlasFramebuffer));
	Camera* sc = dd->mShadowCameras[si];

	sc->Orthographic(ortho);
	if (ortho) sc->OrthographicSize(size);
	else sc->FieldOfView(size);
	sc->Near(near);
	sc->Far(far);
	sc->LocalPosition(pos);
	sc->LocalRotation(rot);
	
	sc->ViewportX((float)((si % (SHADOW_ATLAS_RESOLUTION / SHADOW_RESOLUTION)) * SHADOW_RESOLUTION));
	sc->ViewportY((float)((si / (SHADOW_ATLAS_RESOLUTION / SHADOW_RESOLUTION)) * SHADOW_RESOLUTION));
	sc->ViewportWidth(1024);
	sc->ViewportHeight(1024);

	sd->WorldToShadow = sc->ViewProjection();
	sd->ShadowST = float4(sc->ViewportWidth(), sc->ViewportHeight(), sc->ViewportX(), sc->ViewportY()) / SHADOW_ATLAS_RESOLUTION;
	sd->Proj = float4(sc->Orthographic() ? 1.f : 0.f, 1.f / far, near, far);
};

void Scene::PreFrame(CommandBuffer* commandBuffer) {
	Camera* mainCamera = nullptr;
	for (Camera* c : mCameras)
		if (c->EnabledHierarchy()) {
			mainCamera = c;
			break;
		}
	if (!mainCamera) return;

	Device* device = commandBuffer->Device();
	
	if (mDeviceData.count(device) == 0) {
		DeviceData& data = mDeviceData[device];
		data.mShadowAtlasFramebuffer = new Framebuffer("ShadowAtlas", device, SHADOW_ATLAS_RESOLUTION, SHADOW_ATLAS_RESOLUTION, { VK_FORMAT_R32_SFLOAT }, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT);
		data.mShadowAtlasFramebuffer->BufferUsage(data.mShadowAtlasFramebuffer->BufferUsage() | VK_IMAGE_USAGE_SAMPLED_BIT);
		data.mShadowAtlasFramebuffer->ClearValue(0, { 1.f, 1.f, 1.f, 1.f });
		uint32_t c = device->MaxFramesInFlight();
		data.mLightBuffers = new Buffer*[c];
		data.mShadowBuffers = new Buffer*[c];
		for (uint32_t i = 0; i < c; i++) {
			data.mLightBuffers[i] = new Buffer("Light Buffer", device, MAX_GPU_LIGHTS * sizeof(GPULight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			data.mShadowBuffers[i] = new Buffer("Shadow Buffer", device, MAX_GPU_LIGHTS * sizeof(ShadowData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			data.mLightBuffers[i]->Map();
			data.mShadowBuffers[i]->Map();
		}
	}
	DeviceData& data = mDeviceData.at(device);

	PROFILER_BEGIN("Lighting");
	if (data.mShadowAtlasFramebuffer->ColorBuffer(0))
		data.mShadowAtlasFramebuffer->ColorBuffer(0)->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, commandBuffer);
	data.mShadowAtlasFramebuffer->BeginRenderPass(commandBuffer);

	mActiveLights.clear();
	if (mLights.size()) {
		PROFILER_BEGIN("Gather Lights");
		uint32_t li = 0;
		uint32_t frameContextIndex = device->FrameContextIndex();
		GPULight* lights = (GPULight*)data.mLightBuffers[frameContextIndex]->MappedData();
		ShadowData* shadows = (ShadowData*)data.mShadowBuffers[frameContextIndex]->MappedData();

		uint32_t si = 0;

		float ct = tanf(mainCamera->FieldOfView() * .5f) * max(1.f, mainCamera->Aspect());
		float3 cp = mainCamera->WorldPosition();
		float3 fwd = mainCamera->WorldRotation().forward();

		for (Light* l : mLights) {
			if (!l->EnabledHierarchy()) continue;
			mActiveLights.push_back(l);

			float cosInner = cosf(l->InnerSpotAngle());
			float cosOuter = cosf(l->OuterSpotAngle());

			lights[li].WorldPosition = l->WorldPosition();
			lights[li].InvSqrRange = 1.f / (l->Range() * l->Range());
			lights[li].Color = l->Color() * l->Intensity();
			lights[li].SpotAngleScale = 1.f / fmaxf(.001f, cosInner - cosOuter);
			lights[li].SpotAngleOffset = -cosOuter * lights[li].SpotAngleScale;
			lights[li].Direction = -l->WorldRotation().forward();
			lights[li].Type = l->Type();
			lights[li].ShadowIndex = -1;
			lights[li].CascadeSplits = -1.f;

			if (l->CastShadows()) {
				switch (l->Type()) {
				case Sun: {
					lights[li].CascadeSplits = mCascadeSplits;
					lights[li].ShadowIndex = (int32_t)si;

					for (uint32_t ci = 0; ci < 4; ci++) {
						float mx = mainCamera->Far() * mCascadeSplits[ci];
						float mn = (ci == 0) ? mainCamera->Near() : (mainCamera->Far() * mCascadeSplits[ci - 1]);

						AddShadowCamera(&data, si, &shadows[si], true, ct * mx, cp + fwd * ((mx + mn) * .5f), l->WorldRotation(), -100, 500);
						si++;
					}
					break;
				}
				case Point:
					break;
				case Spot:
					lights[li].CascadeSplits = 1.f;
					lights[li].ShadowIndex = (int32_t)si;
					AddShadowCamera(&data, si, &shadows[si], false, l->OuterSpotAngle() * 2, l->WorldPosition(), l->WorldRotation(), l->Radius() - .001f, l->Range());
					si++;
					break;
				}
			}

			li++;
			if (li >= MAX_GPU_LIGHTS) break;
		}
		PROFILER_END;

		PROFILER_BEGIN("Render Shadows");
		BEGIN_CMD_REGION(commandBuffer, "Render Shadows");
		bool g = mDrawGizmos;
		mDrawGizmos = false;
		for (uint32_t i = 0; i < si; i++)
			Render(data.mShadowCameras[i], commandBuffer, PassType::Depth, false);
		mDrawGizmos = g;
		END_CMD_REGION(commandBuffer);
		PROFILER_END;
	}

	vkCmdEndRenderPass(*commandBuffer);
	data.mShadowAtlasFramebuffer->ColorBuffer(0)->TransitionImageLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer);
	PROFILER_END;

	mGizmos->PreFrame(device);
}

void Scene::Render(Camera* camera, CommandBuffer* commandBuffer, PassType pass, bool startRenderPass) {
	DeviceData& data = mDeviceData.at(commandBuffer->Device());
	
	PROFILER_BEGIN("Gather/Sort Renderers");
	mRenderList.clear();
	for (Renderer* r : mRenderers)
		if (r->Visible() && camera->IntersectFrustum(r->Bounds()) && (pass != Depth || r->CastShadows()))
			mRenderList.push_back(r);
	sort(mRenderList.begin(), mRenderList.end(), [](Renderer* a, Renderer* b) {
		if (a->RenderQueue() == b->RenderQueue())
			if (MeshRenderer* ma = dynamic_cast<MeshRenderer*>(a))
				if (MeshRenderer* mb = dynamic_cast<MeshRenderer*>(b))
					if (ma->Mesh() == mb->Mesh())
						return ma->Material() < mb->Material();
					else
						return ma->Mesh() < mb->Mesh();
		return a->RenderQueue() < b->RenderQueue();
	});
	PROFILER_END;

	camera->PreRender();
	
	PROFILER_BEGIN("Pre Render");
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled)
			p->PreRender(commandBuffer, camera);
	for (Renderer* r : mRenderList)
		r->PreRender(commandBuffer, camera, pass);
	PROFILER_END;

	// combine MeshRenderers that have the same material and mesh
	PROFILER_BEGIN("Draw");
	BEGIN_CMD_REGION(commandBuffer, "Draw Scene");
	if (startRenderPass)
		camera->Framebuffer()->BeginRenderPass(commandBuffer);
	camera->Set(commandBuffer);
	
	DescriptorSet* batchDS = nullptr;
	Buffer* batchBuffer = nullptr;
	ObjectBuffer* curBatch = nullptr;
	MeshRenderer* batchStart = nullptr;
	uint32_t batchSize = 0;

	for (Renderer* r : mRenderList) {
		bool batched = false;
		MeshRenderer* cur = dynamic_cast<MeshRenderer*>(r);
		if (cur) {
			GraphicsShader* curShader = cur->Material()->GetShader(commandBuffer->Device());
			if (curShader->mDescriptorBindings.count("Instances")) {
				if (!batchStart || batchSize + 1 >= INSTANCE_BATCH_SIZE ||
					(batchStart->Material() != cur->Material()) || batchStart->Mesh() != cur->Mesh()) {
					// render last batch
					if (batchStart) {
						BEGIN_CMD_REGION(commandBuffer, "Draw " + batchStart->mName);
						batchStart->DrawInstanced(commandBuffer, camera, batchSize, *batchDS, pass);
						END_CMD_REGION(commandBuffer);
					}

					// start a new batch
					batchSize = 0;
					batchStart = cur;
					
					batchBuffer = commandBuffer->Device()->GetTempBuffer("Instance Batch", sizeof(ObjectBuffer) * INSTANCE_BATCH_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
					batchDS = commandBuffer->Device()->GetTempDescriptorSet("Instance Batch", curShader->mDescriptorSetLayouts[PER_OBJECT]);
					curBatch = (ObjectBuffer*)batchBuffer->MappedData();
					uint32_t frameContextIndex = commandBuffer->Device()->FrameContextIndex();
					batchDS->CreateStorageBufferDescriptor(batchBuffer, 0, batchBuffer->Size(), OBJECT_BUFFER_BINDING);
					if (curShader->mDescriptorBindings.count("Lights"))
						batchDS->CreateStorageBufferDescriptor(data.mLightBuffers[frameContextIndex], 0, data.mLightBuffers[frameContextIndex]->Size(), LIGHT_BUFFER_BINDING);
					if (curShader->mDescriptorBindings.count("Shadows"))
						batchDS->CreateStorageBufferDescriptor(data.mShadowBuffers[frameContextIndex], 0, data.mShadowBuffers[frameContextIndex]->Size(), SHADOW_BUFFER_BINDING);
					if (curShader->mDescriptorBindings.count("ShadowAtlas"))
						batchDS->CreateSampledTextureDescriptor(data.mShadowAtlasFramebuffer->ColorBuffer(0), SHADOW_ATLAS_BINDING);
				}
				// append to batch
				curBatch[batchSize].ObjectToWorld = cur->ObjectToWorld();
				curBatch[batchSize].WorldToObject = cur->WorldToObject();
				batchSize++;
				batched = true;
			}
		}

		if (!batched) {
			// render last batch
			if (batchStart) {
				BEGIN_CMD_REGION(commandBuffer, "Draw " + batchStart->mName);
				batchStart->DrawInstanced(commandBuffer, camera, batchSize, *batchDS, pass);
				END_CMD_REGION(commandBuffer);
				batchStart = nullptr;
			}
			BEGIN_CMD_REGION(commandBuffer, "Draw " + r->mName);
			r->Draw(commandBuffer, camera, pass);
			END_CMD_REGION(commandBuffer);
		}
	}
	// render last batch
	if (batchStart) {
		BEGIN_CMD_REGION(commandBuffer, "Draw " + batchStart->mName);
		batchStart->DrawInstanced(commandBuffer, camera, batchSize, *batchDS, pass);
		END_CMD_REGION(commandBuffer);
	}
	PROFILER_END;

	if (mDrawGizmos) {
		PROFILER_BEGIN("Draw Gizmos");
		BEGIN_CMD_REGION(commandBuffer, "Draw Gizmos");
		for (const auto& r : mObjects)
			if (r->EnabledHierarchy()) {
				BEGIN_CMD_REGION(commandBuffer, "Gizmos " + r->mName);
				r->DrawGizmos(commandBuffer, camera);
				END_CMD_REGION(commandBuffer);
			}

		for (const auto& p : mPluginManager->Plugins())
			if (p->mEnabled)
				p->DrawGizmos(commandBuffer, camera);
		mGizmos->Draw(commandBuffer, camera);
		END_CMD_REGION(commandBuffer);
		PROFILER_END;
	}

	if (startRenderPass)
		vkCmdEndRenderPass(*commandBuffer);
	
	END_CMD_REGION(commandBuffer);
	PROFILER_END;

	// Post Render
	PROFILER_BEGIN("Post Render");
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled)
			p->PostRender(commandBuffer, camera);
	PROFILER_END;

	camera->ResolveWindow(commandBuffer);
}

Collider* Scene::Raycast(const Ray& ray, float& hitT, uint32_t mask) {
	Collider* closest = nullptr;
	hitT = -1.f;

	for (const shared_ptr<Object>& n : mObjects) {
		if (n->EnabledHierarchy()) {
			if (Collider* c = dynamic_cast<Collider*>(n.get())) {
				if ((c->CollisionMask() & mask) != 0) {
					float t = ray.Intersect(c->ColliderBounds()).x;
					if (t > 0 && (t < hitT || closest == nullptr)) {
						closest = c;
						hitT = t;
					}
				}
			}
		}
	}

	return closest;
}