#pragma once

#include <Interface/UIElement.hpp>

#include <Content/Font.hpp>
#include <Content/Material.hpp>
#include <Core/Buffer.hpp>
#include <Core/DescriptorSet.hpp>

#include <cstring>

class UILabel : public UIElement {
public:
	ENGINE_EXPORT UILabel(const std::string& name, UICanvas* canvas);
	ENGINE_EXPORT ~UILabel();

	inline float4 Color() const { return mColor; }
	inline void Color(const float4& c) { mColor = c; }
	
	inline std::string Text() const { return mText; }
	ENGINE_EXPORT void Text(const std::string& text);

	inline TextAnchor HorizontalAnchor() const { return mHorizontalAnchor; }
	inline void HorizontalAnchor(TextAnchor anchor) { mHorizontalAnchor = anchor; for (auto& d : mDeviceData) memset(d.second.mDirty, true, d.first->MaxFramesInFlight() * sizeof(bool)); }

	inline TextAnchor VerticalAnchor() const { return mVerticalAnchor; }
	inline void VerticalAnchor(TextAnchor anchor) { mVerticalAnchor = anchor; for (auto& d : mDeviceData) memset(d.second.mDirty, true, d.first->MaxFramesInFlight() * sizeof(bool)); }

	inline ::Font* Font() const { return mFont.index() == 0 ? std::get<::Font*>(mFont) : std::get<std::shared_ptr<::Font>>(mFont).get(); }
	inline void Font(std::shared_ptr<::Font> f) { mFont = f; for (auto& d : mDeviceData) memset(d.second.mDirty, true, d.first->MaxFramesInFlight() * sizeof(bool)); }
	inline void Font(::Font* f) { mFont = f; for (auto& d : mDeviceData) memset(d.second.mDirty, true, d.first->MaxFramesInFlight() * sizeof(bool)); }

	inline float TextScale() const { return mTextScale; }
	inline void TextScale(float sc) { mTextScale = sc; for (auto& d : mDeviceData) memset(d.second.mDirty, true, d.first->MaxFramesInFlight() * sizeof(bool)); }
	
	ENGINE_EXPORT virtual void Draw(CommandBuffer* commandBuffer, Camera* camera, ::Material* materialOverride) override;

private:
	struct DeviceData {
		DescriptorSet** mDescriptorSets;
		Buffer** mGlyphBuffers;
		bool* mDirty;
		uint32_t mGlyphCount;
	};

	std::vector<TextGlyph> mTempGlyphs;
	uint32_t BuildText(Device* device, Buffer*& d);

	Shader* mShader;
	float4 mColor;
	TextAnchor mHorizontalAnchor;
	TextAnchor mVerticalAnchor;
	float mTextScale;
	std::string mText;
	AABB mAABB;
	AABB mTextAABB;
	std::variant<::Font*, std::shared_ptr<::Font>> mFont;

	std::unordered_map<Device*, DeviceData> mDeviceData;
};