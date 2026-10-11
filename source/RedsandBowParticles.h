#pragma once

namespace RE {
	struct BSAnimationGraphEvent;
	template <class T> class BSTEventSource;
}

namespace RedsandBowParticles
{
	void Install();
	void OnAnimationEvent(const RE::BSAnimationGraphEvent* event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* source);
	void SetEnabled(bool enabled);
	void Update(float deltaTime);
}
