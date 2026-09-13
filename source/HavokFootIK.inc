					const auto* characterData = character->setup ? character->setup->data.get() : nullptr;
					const bool hasFootIKInfo = characterData &&
						(REL::Module::IsSE() ? RedsandHavokLayout::HasSEFootIKDriverInfo(characterData) :
						                     characterData->footIkDriverInfo.get() != nullptr);
					if (character->footIkDriver && hasFootIKInfo) {
