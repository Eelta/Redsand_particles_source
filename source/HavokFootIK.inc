					const auto* characterData = character->setup ? character->setup->data.get() : nullptr;
					const bool hasFootIKInfo = RedsandHavokLayout::HasFootIKDriverInfo(characterData);
					if (character->footIkDriver && hasFootIKInfo) {
