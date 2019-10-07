#include <glm/gtx/quaternion.hpp>

#include <Core/DescriptorSet.hpp>
#include <Scene/Camera.hpp>
#include <Shaders/shadercompat.h>

#include "PointRenderer.hpp"

using namespace std;

PointRenderer::PointRenderer(const string& name) : Renderer(name), mVisible(true) {}
PointRenderer::~PointRenderer() {
	for (auto& d : mDeviceData) {
		for (uint32_t i = 0; i < d.first->MaxFramesInFlight(); i++) {
			safe_delete(d.second.mObjectBuffers[i]);
			safe_delete(d.second.mDescriptorSets[i]);
		}
		safe_delete(d.second.mPointBuffer);
		safe_delete_array(d.second.mDescriptorDirty);
		safe_delete_array(d.second.mObjectBuffers);
		safe_delete_array(d.second.mDescriptorSets);
	}
}

void PointRenderer::Points(const vector<Point>& points) {
	mPoints = points;
	if (!points.size()) return;

	vec3 mn = vec3(points[0].mPosition);
	vec3 mx = vec3(points[0].mPosition);
	for (uint32_t i = 1; i < points.size(); i++) {
		mn = gmin(vec3(points[i].mPosition), mn);
		mx = gmax(vec3(points[i].mPosition), mx);
	}
	mPointAABB = AABB((mn + mx) * .5f, (mx - mn) * .5f);

	for (auto& d : mDeviceData) {
		memset(d.second.mDescriptorDirty, true, d.first->MaxFramesInFlight() * sizeof(bool));
		d.second.mPointsDirty = true;
	}
	mAABB = AABB(mPointAABB, ObjectToWorld());
}

void PointRenderer::Draw(const FrameTime& frameTime, Camera* camera, CommandBuffer* commandBuffer, uint32_t backBufferIndex, ::Material* materialOverride) {
	if (!mPoints.size()) return;

	::Material* material = materialOverride ? materialOverride : mMaterial.get();
	if (!material) return;

	VkPipelineLayout layout = commandBuffer->BindMaterial(material, backBufferIndex, nullptr);
	if (!layout) return;

	if (!mDeviceData.count(commandBuffer->Device())) {
		DeviceData& d = mDeviceData[commandBuffer->Device()];
		uint32_t fc = commandBuffer->Device()->MaxFramesInFlight();
		d.mObjectBuffers = new Buffer*[fc];
		d.mDescriptorSets = new DescriptorSet*[fc];
		d.mDescriptorDirty = new bool[fc];
		d.mPointsDirty = true;
		d.mPointBuffer = nullptr;
		memset(d.mObjectBuffers, 0, sizeof(Buffer*) * fc);
		memset(d.mDescriptorSets, 0, sizeof(DescriptorSet*) * fc);
		memset(d.mDescriptorDirty, true, sizeof(bool) * fc);
	}

	DeviceData& data = mDeviceData.at(commandBuffer->Device());

	if (data.mPointsDirty) {
		commandBuffer->Device()->FlushCommandBuffers();
		safe_delete(data.mPointBuffer);
	}

	if (!data.mPointBuffer) {
		data.mPointBuffer = new Buffer(mName + " PointBuffer", commandBuffer->Device(), mPoints.data(), sizeof(Point) * mPoints.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		data.mPointsDirty = false;
	}
	if (!data.mObjectBuffers[backBufferIndex]) {
		data.mObjectBuffers[backBufferIndex] = new Buffer(mName + " ObjectBuffer", commandBuffer->Device(), sizeof(ObjectBuffer), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		data.mObjectBuffers[backBufferIndex]->Map();
	}
	if (!data.mDescriptorSets[backBufferIndex]) {
		auto shader = material->GetShader(commandBuffer->Device());
		data.mDescriptorSets[backBufferIndex] = new DescriptorSet(mName + " PerObject DescriptorSet", commandBuffer->Device()->DescriptorPool(), shader->mDescriptorSetLayouts[PER_OBJECT]);
		data.mDescriptorSets[backBufferIndex]->CreateUniformBufferDescriptor(data.mObjectBuffers[backBufferIndex], OBJECT_BUFFER_BINDING);
	}
	if (data.mDescriptorDirty[backBufferIndex]) {
		auto& bindings = material->GetShader(commandBuffer->Device())->mDescriptorBindings;
		if (bindings.count("Points")) {
			data.mDescriptorSets[backBufferIndex]->CreateSRVBufferDescriptor(data.mPointBuffer, bindings.at("Points").second.binding);
			data.mDescriptorDirty[backBufferIndex] = false;
		}
	}

	ObjectBuffer* objbuffer = (ObjectBuffer*)data.mObjectBuffers[backBufferIndex]->MappedData();
	objbuffer->ObjectToWorld = ObjectToWorld();
	objbuffer->WorldToObject = WorldToObject();

	VkDescriptorSet camds = *camera->DescriptorSet(backBufferIndex);
	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_CAMERA, 1, &camds, 0, nullptr);

	VkDescriptorSet objds = *data.mDescriptorSets[backBufferIndex];
	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, &objds, 0, nullptr);

	vkCmdDraw(*commandBuffer, 6 * mPoints.size(), 1, 0, 0);
}

bool PointRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	mAABB = AABB(mPointAABB, ObjectToWorld());
	return true;
}