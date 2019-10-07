#include <Util/Profiler.hpp>

using namespace std;

ProfilerSample  Profiler::mFrames[PROFILER_FRAME_COUNT];
ProfilerSample* Profiler::mCurrentSample = nullptr;
uint64_t Profiler::mCurrentFrame = 0;
const std::chrono::high_resolution_clock Profiler::mTimer;

void Profiler::BeginSample(const string& label, bool resume = false) {
	if (resume) {
		for (auto& c : mCurrentSample->mChildren)
			if (c.mLabel == label) {
				mCurrentSample = &c;
				c.mStartTime = mTimer.now();
				return;
			}
	}
	mCurrentSample->mChildren.push_back({ label, mCurrentSample, {} });
	mCurrentSample = &mCurrentSample->mChildren.back();
	mCurrentSample->mStartTime = mTimer.now();
}
void Profiler::EndSample() {
	if (!mCurrentSample->mParent) throw runtime_error("Mismatch between BeginSample() and EndSample()!");
	mCurrentSample->mTime += mTimer.now() - mCurrentSample->mStartTime;
	mCurrentSample = mCurrentSample->mParent;
}

void Profiler::FrameStart() {
	int i = mCurrentFrame % PROFILER_FRAME_COUNT;
	mFrames[i].mLabel = "Frame " + to_string(mCurrentFrame);
	mFrames[i].mParent = nullptr;
	mFrames[i].mStartTime = mTimer.now();
	mFrames[i].mTime = chrono::nanoseconds::zero();
	mFrames[i].mChildren.clear();
	mCurrentSample = &mFrames[i];
}
void Profiler::FrameEnd() {
	mCurrentFrame++;
}

void PrintSample(char* buffer, size_t size, size_t& c, ProfilerSample* s, uint32_t tabLevel) {
	for (uint32_t i = 0; i < tabLevel; i++)
		c += sprintf_s(buffer + c, size - c, "  ");

	c += sprintf_s(buffer + c, size - c, "%s: %.2fms\n", s->mLabel.c_str(), s->mTime.count() * 1e-6);
	for (auto& pc : s->mChildren)
		PrintSample(buffer, size, c, &pc, tabLevel + 1);
}
void Profiler::PrintLastFrame(char* buffer, size_t size) {
	size_t c = 0;
	PrintSample(buffer, size, c, &mFrames[(mCurrentFrame + PROFILER_FRAME_COUNT - 1) % PROFILER_FRAME_COUNT], 1);
}