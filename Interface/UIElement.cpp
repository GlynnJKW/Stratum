#include <Interface/UIElement.hpp>

#include <Interface/UICanvas.hpp>

using namespace std;

UIElement::UIElement(const string& name)
	: mName(name), mCanvas(nullptr), mParent(nullptr), mPosition(UDim2()), mExtent(UDim2()), mDepth(0.f), mAbsolutePosition(vec3()), mAbsoluteExtent(vec2()), mTransformDirty(true), mVisible(true) {}
UIElement::~UIElement() {}

bool UIElement::Parent(UIElement* p) {
	if (mParent == p) return true;

	if (mParent)
		for (auto it = mParent->mChildren.begin(); it != mParent->mChildren.end();)
			if (*it == this)
				it = mParent->mChildren.erase(it);
			else
				it++;

	mParent = p;
	if (!p || !p->AddChild(this)) {
		Dirty();
		return false;
	}
	Dirty();
	return true;
}
bool UIElement::AddChild(UIElement* e) {
	mChildren.push_back(e);
	Dirty();
	return true;
}

bool UIElement::UpdateTransform() {
	if (!mTransformDirty) return false;

	if (mParent) {
		mAbsolutePosition = mParent->AbsolutePosition() + vec3(mParent->AbsoluteExtent() * mPosition.mScale + mPosition.mOffset, mDepth);
		mAbsoluteExtent   = mParent->AbsoluteExtent() * mExtent.mScale + mExtent.mOffset;
	} else {
		mAbsolutePosition = vec3(mCanvas->Extent() * mPosition.mScale + mPosition.mOffset, mDepth);
		mAbsoluteExtent = mCanvas->Extent() * mExtent.mScale + mExtent.mOffset;
	}

	mAbsoluteAABB = AABB(mAbsolutePosition + vec3(mAbsoluteExtent, 0), vec3(mAbsoluteExtent, UI_THICKNESS));
	for (UIElement*& e : mChildren)
		mAbsoluteAABB.Encapsulate(e->AbsoluteBounds());

	mTransformDirty = false;
	return true;
}

void UIElement::Dirty() {
	mTransformDirty = true;
	for (const auto& o : mChildren)
		o->Dirty();
}