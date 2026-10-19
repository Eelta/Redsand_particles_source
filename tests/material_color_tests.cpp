#include "../source/RedsandMaterialColor.h"
#include "../source/RedsandSlashRandom.h"
#include <iostream>
#include <stdexcept>

struct Color { float red, green, blue, alpha; };
struct Material {
	Color baseColor{1, .72f, .24f, .37f}; float baseColorScale = 8.f;
	int texture = 19, greyscale = 0;
	bool createFails = false;
	Material* Create() { return createFails ? nullptr : new Material; }
	void CopyMembers(Material* other) { *this = *other; }
};
struct Shader {
	Material value;

	Material renderPass;
	int commits = 0;
	void SetMaterial(Material* copy, bool unique) {
		if (!unique) { throw std::runtime_error("shared material was published"); }
		value = renderPass = *copy; ++commits;
	}
	Material* GetMaterial() { return &value; }
};
void Check(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }
int main()
{
	try {
		using RedsandMaterialColor::Commit;
		Shader slash, other;
		auto* material = slash.GetMaterial();
		int released = 0;
		auto release = [&](Material* copy) { ++released; delete copy; };
		const std::array<float,3> rgb{ .11f, .24f, .65f };
		Check(Commit(&slash, material, rgb, 80.f, true, release), "initial commit failed");
		Check(slash.renderPass.baseColor.red == rgb[0] && slash.renderPass.baseColor.blue == rgb[2],
			"particle render pass captured the old gold color");
		Check(slash.renderPass.baseColor.alpha == .37f && slash.renderPass.texture == 19 && slash.renderPass.baseColorScale == 80.f,
			"alpha/texture/glow changed while publishing color");
		Check(other.value.baseColor.red == 1.f && other.commits == 0, "another effect was recolored");
		for (int i=0;i<10000;++i) { Check(Commit(&slash, material, rgb, 80.f, false, release), "stable update failed"); }
		Check(slash.commits == 1 && released == 1, "steady playback cloned materials every frame");
		material->baseColor.alpha = .12f;
		Check(Commit(&slash, material, { .025f, .08f, .65f }, std::nullopt, false, release), "depletion transition failed");
		Check(slash.renderPass.baseColor.alpha == .12f && slash.renderPass.baseColorScale == 80.f, "animated alpha or glow was reset");
		Check(slash.renderPass.baseColor.red == .025f && slash.renderPass.baseColor.green == .08f, "blue was not published");
		material->createFails = true;
		const auto old = material->baseColor;
		Check(!Commit(&slash, material, {1,0,1}, 20.f, false, release), "allocation failure was ignored");
		Check(material->baseColor.red == old.red && material->baseColor.alpha == old.alpha && released == 2,
			"failure changed or leaked the existing material");


        Shader contact;
        auto* contactMaterial = contact.GetMaterial();
        RedsandSettings::Config config; config.slashRandomColor = true;
        const auto late = RedsandSlashRandom::ReadSceneSeed(-173.f);
        const auto local = RedsandSlashRandom::Sample(config, late->value, late->contact);
        const auto cut = RedsandSlashRandom::Sample(config, late->value);
        const auto wanted = local.colors[static_cast<unsigned>(RedsandSettings::ColorTarget::Hit)].rgb;
        Check(wanted == cut.colors[static_cast<unsigned>(RedsandSettings::ColorTarget::Cut)].rgb,
            "late paired contact did not match its cut color");
        for (unsigned frame=0; frame<10000; ++frame) {
            Check(Commit(&contact, contactMaterial, wanted, std::nullopt, false, release), "late draw tint failed");
            Check(contact.renderPass.baseColor.red == wanted[0] && contact.renderPass.baseColor.green == wanted[1] &&
                contact.renderPass.baseColor.blue == wanted[2], "late draw captured the initial gold color");
        }
        Check(contact.commits == 1 && contact.value.baseColor.alpha == .37f && contact.value.texture == 19 &&
            contact.value.baseColorScale == 8.f, "late correction changed fade/texture/glow or allocated every frame");
		std::cout << "Render-pass color capture, private material isolation, animated alpha preservation, allocation failure and steady-state reuse passed\n";
	} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
