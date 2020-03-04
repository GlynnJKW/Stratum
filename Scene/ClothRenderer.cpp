#include <Core/DescriptorSet.hpp>
#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Environment.hpp>
#include <Util/Profiler.hpp>

#include <Shaders/include/shadercompat.h>

#include "ClothRenderer.hpp"

using namespace std;

ClothRenderer::ClothRenderer(const string& name)
	:  MeshRenderer(name), Object(name), mVertexBuffer(nullptr), mLastVertexBuffer(nullptr), mForceBuffer(nullptr), mCopyVertices(false), mDrag(10), mStiffness(1) {}
ClothRenderer::~ClothRenderer() { safe_delete(mVertexBuffer); safe_delete(mLastVertexBuffer); safe_delete(mForceBuffer); }

void ClothRenderer::Mesh(::Mesh* m) {
	mMesh = m;
	Dirty();

	safe_delete(mVertexBuffer);
	safe_delete(mLastVertexBuffer);
	safe_delete(mForceBuffer);
	if (m) {
		mVertexBuffer = new Buffer(mName + "Vertices", m->VertexBuffer()->Device(), m->VertexCount() * m->VertexSize(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mLastVertexBuffer = new Buffer(mName + "PrevVertices", m->VertexBuffer()->Device(), m->VertexCount() * m->VertexSize(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mForceBuffer = new Buffer(mName + "Forces", m->VertexBuffer()->Device(), sizeof(float4) * m->VertexCount(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mCopyVertices = true;
	}
}
void ClothRenderer::Mesh(std::shared_ptr<::Mesh> m) {
	mMesh = m;
	Dirty();

	safe_delete(mVertexBuffer);
	safe_delete(mLastVertexBuffer);
	safe_delete(mForceBuffer);
	if (m) {
		mVertexBuffer = new Buffer(mName + "Vertices", m->VertexBuffer()->Device(), m->VertexBuffer()->Size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mLastVertexBuffer = new Buffer(mName + "Vertices", m->VertexBuffer()->Device(), m->VertexBuffer()->Size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mForceBuffer = new Buffer(mName + "Forces", m->VertexBuffer()->Device(), sizeof(float4) * m->VertexCount(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mCopyVertices = true;
	}
}

void ClothRenderer::FixedUpdate(CommandBuffer* commandBuffer) {
	if (!mVertexBuffer) return;
	::Mesh* m = MeshRenderer::Mesh();

	VkBufferMemoryBarrier b = {};
	b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

	if (mCopyVertices) {
		VkBufferCopy cpy = {};
		cpy.srcOffset = m->VertexSize() * m->BaseVertex();
		cpy.size = mVertexBuffer->Size();
		vkCmdCopyBuffer(*commandBuffer, *m->VertexBuffer(), *mVertexBuffer, 1, &cpy);
		vkCmdCopyBuffer(*commandBuffer, *m->VertexBuffer(), *mLastVertexBuffer, 1, &cpy);

		b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		b.buffer = *mVertexBuffer;
		b.size = mVertexBuffer->Size();
		vkCmdPipelineBarrier(*commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &b, 0, nullptr);

		b.buffer = *mLastVertexBuffer;
		vkCmdPipelineBarrier(*commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &b, 0, nullptr);

		mCopyVertices = false;
	}

	vkCmdFillBuffer(*commandBuffer, *mForceBuffer, 0, mForceBuffer->Size(), 0);
	b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	b.buffer = *mForceBuffer;
	b.size = mForceBuffer->Size();
	vkCmdPipelineBarrier(*commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &b, 0, nullptr);
	b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;


	Buffer* objBuffer = commandBuffer->Device()->GetTempBuffer("Cloth Obj", sizeof(float4x4) * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	((float4x4*)objBuffer->MappedData())[0] = ObjectToWorld();
	((float4x4*)objBuffer->MappedData())[1] = WorldToObject();

	Shader* shader = Scene()->AssetManager()->LoadShader("Shaders/cloth.stm");

	uint32_t vc = m->VertexCount();
	uint32_t no = offsetof(StdVertex, normal);
	uint32_t vs = sizeof(StdVertex);

	float4x4 o2w = ObjectToWorld();
	float4x4 w2o = WorldToObject();
	float dt = Scene()->FixedTimeStep();
	VkDeviceSize vsize = m->VertexSize();

	VkDeviceSize baseVertex = m->BaseVertex() * m->VertexSize();
	VkDeviceSize baseIndex = m->BaseIndex() * (m->IndexType() == VK_INDEX_TYPE_UINT16 ? sizeof(uint16_t) : sizeof(uint32_t));

	ComputeShader* add = m->IndexType() == VK_INDEX_TYPE_UINT16 ? Scene()->AssetManager()->LoadShader("Shaders/cloth.stm")->GetCompute("AddForces", {}) : Scene()->AssetManager()->LoadShader("Shaders/cloth.stm")->GetCompute("AddForces", {"INDEX_UINT32"});
	DescriptorSet* ds = new DescriptorSet("AddForces", Scene()->Instance()->Device(), add->mDescriptorSetLayouts[0]);
	ds->CreateUniformBufferDescriptor(objBuffer, 0, objBuffer->Size(), add->mDescriptorBindings.at("ObjectBuffer").second.binding);
	ds->CreateStorageBufferDescriptor(m->VertexBuffer().get(), baseVertex, m->VertexBuffer()->Size() - baseVertex, add->mDescriptorBindings.at("SourceVertices").second.binding);
	ds->CreateStorageBufferDescriptor(m->IndexBuffer().get(), baseIndex, m->IndexBuffer()->Size() - baseIndex, add->mDescriptorBindings.at("Triangles").second.binding);
	ds->CreateStorageBufferDescriptor(mVertexBuffer, 0, mVertexBuffer->Size(), add->mDescriptorBindings.at("Vertices").second.binding);
	ds->CreateStorageBufferDescriptor(mLastVertexBuffer, 0, mLastVertexBuffer->Size(), add->mDescriptorBindings.at("LastVertices").second.binding);
	ds->CreateStorageBufferDescriptor(mForceBuffer, 0, mForceBuffer->Size(), add->mDescriptorBindings.at("Forces").second.binding);
	ds->FlushWrites();
	vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, add->mPipeline);
	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, add->mPipelineLayout, 0, 1, *ds, 0, nullptr);
	commandBuffer->PushConstant(add, "ObjectToWorld", &o2w);
	commandBuffer->PushConstant(add, "WorldToObject", &w2o);
	commandBuffer->PushConstant(add, "VertexSize", &vsize);
	commandBuffer->PushConstant(add, "Drag", &mDrag);
	commandBuffer->PushConstant(add, "Stiffness", &mStiffness);
	commandBuffer->PushConstant(add, "DeltaTime", &dt);
	vkCmdDispatch(*commandBuffer, ((m->IndexCount() / 3 + 63) / 64), 1, 1);


	b.buffer = *mForceBuffer;
	b.size = mForceBuffer->Size();
	vkCmdPipelineBarrier(*commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &b, 0, nullptr);


	ComputeShader* integrate = Scene()->AssetManager()->LoadShader("Shaders/cloth.stm")->GetCompute("Integrate", {});
	ds = new DescriptorSet("Integrate", Scene()->Instance()->Device(), integrate->mDescriptorSetLayouts[0]);
	ds->CreateUniformBufferDescriptor(objBuffer, 0, objBuffer->Size(), integrate->mDescriptorBindings.at("ObjectBuffer").second.binding);
	ds->CreateStorageBufferDescriptor(mVertexBuffer, 0, mVertexBuffer->Size(), integrate->mDescriptorBindings.at("Vertices").second.binding);
	ds->CreateStorageBufferDescriptor(mLastVertexBuffer, 0, mLastVertexBuffer->Size(), integrate->mDescriptorBindings.at("LastVertices").second.binding);
	ds->CreateStorageBufferDescriptor(mForceBuffer, 0, mForceBuffer->Size(), integrate->mDescriptorBindings.at("Forces").second.binding);
	ds->FlushWrites();
	vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, integrate->mPipeline);
	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, integrate->mPipelineLayout, 0, 1, *ds, 0, nullptr);
	commandBuffer->PushConstant(integrate, "ObjectToWorld", &o2w);
	commandBuffer->PushConstant(integrate, "WorldToObject", &w2o);
	commandBuffer->PushConstant(add, "VertexSize", &vsize);
	commandBuffer->PushConstant(integrate, "DeltaTime", &dt);
	vkCmdDispatch(*commandBuffer, ((m->IndexCount() / 3 + 63) / 64), 1, 1);


	b.buffer = *mVertexBuffer;
	b.size = mVertexBuffer->Size();
	vkCmdPipelineBarrier(*commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &b, 0, nullptr);
}

void ClothRenderer::PreRender(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {
	if (!mVertexBuffer) return;

	VkBufferMemoryBarrier b = {};
	b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	b.buffer = *mVertexBuffer;
	b.size = mVertexBuffer->Size();
	vkCmdPipelineBarrier(*commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &b, 0, nullptr);
}
void ClothRenderer::DrawInstanced(CommandBuffer* commandBuffer, Camera* camera, uint32_t instanceCount, VkDescriptorSet instanceDS, PassType pass) {
	::Mesh* mesh = MeshRenderer::Mesh();

	VkCullModeFlags cull = (pass == PASS_DEPTH) ? VK_CULL_MODE_NONE : VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
	VkPipelineLayout layout = commandBuffer->BindMaterial(mMaterial.get(), pass, mesh->VertexInput(), camera, mesh->Topology(), cull);
	if (!layout) return;
	auto shader = mMaterial->GetShader(pass);

	for (const auto& kp : mPushConstants)
		commandBuffer->PushConstant(shader, kp.first, &kp.second);

	uint32_t lc = (uint32_t)Scene()->ActiveLights().size();
	float2 s = Scene()->ShadowTexelSize();
	float t = Scene()->TotalTime();
	commandBuffer->PushConstant(shader, "Time", &t);
	commandBuffer->PushConstant(shader, "LightCount", &lc);
	commandBuffer->PushConstant(shader, "ShadowTexelSize", &s);
	for (const auto& kp : mPushConstants)
		commandBuffer->PushConstant(shader, kp.first, &kp.second);

	if (instanceDS != VK_NULL_HANDLE)
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, &instanceDS, 0, nullptr);

	commandBuffer->BindVertexBuffer(mVertexBuffer, 0, 0);
	commandBuffer->BindIndexBuffer(mesh->IndexBuffer().get(), 0, mesh->IndexType());
	camera->SetStereo(commandBuffer, shader, EYE_LEFT);
	vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), instanceCount, mesh->BaseIndex(), 0, 0);
	commandBuffer->mTriangleCount += instanceCount * (mesh->IndexCount() / 3);

	if (camera->StereoMode() != STEREO_NONE) {
		camera->SetStereo(commandBuffer, shader, EYE_RIGHT);
		vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), instanceCount, mesh->BaseIndex(), 0, 0);
		commandBuffer->mTriangleCount += instanceCount * (mesh->IndexCount() / 3);
	}
}

bool ClothRenderer::Intersect(const Ray& ray, float* t, bool any) {
	return false;
}