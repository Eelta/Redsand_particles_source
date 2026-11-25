#include <bit>
#include <cstring>
#include <d3d11.h>
#include "RE/B/BSResourceNiBinaryStream.h"
#include "RE/B/BSStream.h"

namespace
{
	constexpr auto redsandSwingMesh = "RedsandSwingTrail/Arc.nif";

	RedsandSwingTrail::Vec3 SwingVector(const RE::NiPoint3& point)
	{
		return {point.x, point.y, point.z};
	}

	void GatherRedsandBladeTip(RE::NiAVObject& object, RE::NiTransform parent, unsigned depth,
		RedsandSwingTrail::BladeTip& vertices, RedsandSwingTrail::BladeTip& bounds, unsigned& remaining)
	{
		if (!depth || !remaining || object.GetAppCulled() || !RedsandSwingTrail::IsBladePart(object.name.c_str())) { return; }
		const auto local = parent * object.local;
		auto* shape = object.AsTriShape();
		if (!shape) {
			if (auto* node = object.AsNode()) {
				for (auto& child : node->children) {
					if (child) { GatherRedsandBladeTip(*child, local, depth - 1, vertices, bounds, remaining); }
				}
			}
			return;
		}

		const auto& geometry = shape->GetGeometryRuntimeData();
		if (geometry.skinInstance || shape->GetType().get() != RE::BSGeometry::Type::kTriShape) { return; }
		if (const auto* shader = geometry.shaderProperty.get(); shader &&
			shader->flags.none(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferWrite)) { return; }
		const auto& model = shape->GetModelData().modelBound;
		const auto center = local * model.center;
		const float length = center.Length();
		if (length > 1.f && std::isfinite(model.radius) && model.radius > 0.f) {
			bounds.Consider(SwingVector(center * (1.f + std::abs(local.scale) * model.radius / length)));
		}

		const auto* renderer = geometry.rendererData;
		auto descriptor = geometry.vertexDesc;
		const auto stride = static_cast<unsigned>(std::bit_cast<std::uint64_t>(descriptor) & 0xFu) * 4u;
		if (!renderer || !renderer->rawVertexData || !descriptor.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX) ||
			stride < 16u || stride != descriptor.GetSize() || !(renderer->vertexDesc.GetFlags() & RE::BSGraphics::Vertex::VF_VERTEX)) { return; }
		const auto rawStride = static_cast<unsigned>(std::bit_cast<std::uint64_t>(renderer->vertexDesc) & 0xFu) * 4u;
		if (rawStride != stride) { return; }
		const unsigned count = std::min<unsigned>(shape->GetTrishapeRuntimeData().vertexCount, remaining);
		remaining -= count;
		auto inside = 0u;
		RedsandSwingTrail::BladeTip current;
		for (unsigned i = 0; i < count; ++i) {
			std::array<float, 3> coord{};
			std::memcpy(coord.data(), renderer->rawVertexData + i * stride, sizeof(coord));
			const RE::NiPoint3 point{coord[0], coord[1], coord[2]};
			const float distance = (point - model.center).Length();
			if (std::isfinite(distance) && distance <= model.radius * 1.05f + .25f) {
				++inside;
				current.Consider(SwingVector(local * point));
			}
		}
		if (inside && inside >= count * 9u / 10u) { vertices.Consider(current.point); }
	}

	RE::NiParticleSystem* FindRedsandSwingSystem(RE::NiAVObject* root)
	{
		RE::NiParticleSystem* result = nullptr;
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geometry) {
			if (geometry->name == "RedsandParticles_Drain_v1"sv) {
				result = netimmerse_cast<RE::NiParticleSystem*>(geometry);
				if (result) { return RE::BSVisit::BSVisitControl::kStop; }
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return result;
	}

	const RE::NiPointer<RE::NiNode>& RedsandSwingTemplate()
	{
		static const auto model = [] {
			RE::NiPointer<RE::NiNode> loaded;
			if (RE::BSModelDB::Demand(redsandSwingMesh, loaded, {}) != RE::BSResource::ErrorCode::kNone) { loaded.reset(); }
			return loaded;
		}();
		return model;
	}

	RE::NiPointer<RE::NiNode> LoadRedsandRibbonFromResource()
	{
		if (REL::Module::get().version() != REL::Version{1, 5, 97, 0}) { return {}; }

		// Skyrim SE inlines BSStream construction in its model reader. Use the same
		// native base constructor, BSStream vtable, stream reader and destructor.
		const REL::Relocation<RE::NiStream* (*)(void*)> construct{REL::ID{68971}};
		const REL::Relocation<void (*)(RE::NiStream*)> destroy{REL::ID{13007}};
		const REL::Relocation<bool (*)(RE::NiStream*, const char*, RE::NiBinaryStream*)> load{REL::ID{74207}};
		const REL::Relocation<const std::uintptr_t*> vtable{RE::VTABLE_BSStream[0]};
		const REL::Relocation<std::uintptr_t> loadPath{REL::ID{74208}};
		constexpr std::array ctorPrefix{0x48u, 0x89u, 0x4Cu, 0x24u, 0x08u, 0x57u, 0x48u, 0x83u, 0xECu, 0x40u};
		constexpr std::array dtorPrefix{0x40u, 0x57u, 0x48u, 0x83u, 0xECu, 0x30u, 0x48u, 0xC7u};
		constexpr std::array loadPrefix{0x40u, 0x56u, 0x57u, 0x41u, 0x56u, 0x48u, 0x83u, 0xECu, 0x30u};
		const auto matches = [](std::uintptr_t address, const auto& prefix) {
			return std::equal(prefix.begin(), prefix.end(), reinterpret_cast<const std::uint8_t*>(address));
		};
		if (vtable.get()[3] != loadPath.address() || !matches(construct.address(), ctorPrefix) ||
			!matches(destroy.address(), dtorPrefix) || !matches(load.address(), loadPrefix)) {
			RedsandLog::info("Redsand ribbon native stream bindings unavailable");
			return {};
		}

		constexpr auto resourcePath = "meshes\\RedsandSwingTrail\\Ribbon.nif";
		RE::BSResourceNiBinaryStream resource(resourcePath);
		if (!resource.good()) {
			RedsandLog::info("Redsand ribbon resource stream failed: status={}", static_cast<int>(resource.lastError));
			return {};
		}
		alignas(RE::BSStream) std::byte storage[sizeof(RE::BSStream)]{};
		auto* stream = construct(storage);
		const auto table = vtable.address();
		std::memcpy(storage, &table, sizeof(table));
		const std::unique_ptr<RE::NiStream, decltype(destroy.get())> release(stream, destroy.get());
		const bool loaded = load(stream, resourcePath, &resource);
		if (loaded && stream->topObjects.size() == 1 && stream->topObjects.front()) {
			if (auto* root = stream->topObjects.front()->AsNode(); root && root->name == "RedsandRibbonPool"sv) {
				RedsandLog::info("Redsand ribbon NIF loaded through native resource stream: children={}", root->children.size());
				return RE::NiPointer<RE::NiNode>(root);
			}
		}
		const char* message = stream->lastErrorMessage;
		const auto length = std::find(message, message + sizeof(stream->lastErrorMessage), '\0') - message;
		RedsandLog::info("Redsand ribbon native NIF load failed: parsed={} error={} detail={} roots={}",
			loaded, stream->lastError, std::string_view(message, length), stream->topObjects.size());
		return {};
	}

	const auto& RedsandRibbonPool()
	{
		using Pool = std::array<RE::NiPointer<RE::BSTriShape>, RedsandSwingTrail::Lease::maximum>;
		static const auto pool = []() -> Pool {
			auto root = LoadRedsandRibbonFromResource();
			if (!root) {
				RE::BSModelDB::DBTraits::ArgsType args;
				args.postProcess = false;
				const auto status = RE::BSModelDB::Demand("RedsandSwingTrail\\Ribbon.nif", root, args);
				if (status != RE::BSResource::ErrorCode::kNone || !root || root->name != "RedsandRibbonPool"sv) {
					RedsandLog::info("Redsand ribbon model database load failed: status={} root={}",
						static_cast<int>(status), static_cast<bool>(root));
					return {};
				}
			}
			Pool shapes;
			unsigned accepted = 0;
			unsigned withBuffers = 0;
			for (std::size_t i = 0; i < shapes.size(); ++i) {
				const RE::BSFixedString name{fmt::format("RedsandRibbon{:02}", i)};
				auto* child = root->GetObjectByName(name);
				auto* shape = child ? child->AsTriShape() : nullptr;
				if (!shape) {
					if (i == 0) { RedsandLog::info("Redsand ribbon slot 0 shape missing: children={}", root->children.size()); }
					continue;
				}
				const auto& geometry = shape->GetGeometryRuntimeData();
				const auto& counts = shape->GetTrishapeRuntimeData();
				if (!geometry.shaderProperty || geometry.skinInstance ||
					counts.vertexCount != RedsandSwingTrail::Simulation::maxRibbonRows * 3 ||
					counts.triangleCount != (RedsandSwingTrail::Simulation::maxRibbonRows - 1) * 4) {
					if (i == 0) { RedsandLog::info("Redsand ribbon slot 0 layout mismatch: vertices={} faces={} shader={} skinned={}",
						counts.vertexCount, counts.triangleCount, static_cast<bool>(geometry.shaderProperty), static_cast<bool>(geometry.skinInstance)); }
					continue;
				}
				shapes[i].reset(shape);
				++accepted;
				if (geometry.rendererData && geometry.rendererData->vertexBuffer && geometry.rendererData->indexBuffer) { ++withBuffers; }
			}
			for (auto& shape : shapes) {
				if (!shape) { continue; }
				shape->SetAppCulled(true);
				if (auto* parent = shape->parent) { parent->DetachChild(shape.get()); }
			}
			RedsandLog::info("Redsand ribbon slots loaded: {} of {} ({} with GPU buffers)", accepted, shapes.size(), withBuffers);
			return shapes;
		}();
		return pool;
	}

	struct RedsandRibbonVertex
	{
		RE::NiPoint3 position;
		float unused = 0.f;
		std::uint16_t u = 0, v = 0x3800;
		std::uint8_t r = 255, g = 255, b = 255, a = 0;
	};
	static_assert(sizeof(RedsandRibbonVertex) == 24 && offsetof(RedsandRibbonVertex, a) == 23);

	class RedsandRibbonRenderLock
	{
	public:
		explicit RedsandRibbonRenderLock(RE::BSGraphics::Renderer& renderer) : renderer(renderer) { renderer.Lock(); }
		~RedsandRibbonRenderLock() { renderer.Unlock(); }
		RedsandRibbonRenderLock(const RedsandRibbonRenderLock&) = delete;
		RedsandRibbonRenderLock& operator=(const RedsandRibbonRenderLock&) = delete;
	private:
		RE::BSGraphics::Renderer& renderer;
	};

	bool WriteRedsandRibbon(RE::BSTriShape& shape, const std::vector<RedsandSwingTrail::RibbonRow>& rows,
		const std::array<float, 3>& tint)
	{
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		auto* data = RE::BSGraphics::Renderer::GetRendererDataSingleton();
		if (!renderer || !data || !data->context || !data->forwarder || shape.world.scale <= .0001f) { return false; }
		auto& geometry = shape.GetGeometryRuntimeData();
		auto* bufferData = geometry.rendererData;
		using Vertex = RE::BSGraphics::Vertex;
		if (!bufferData || !bufferData->indexBuffer || bufferData->vertexDesc.GetSize() != sizeof(RedsandRibbonVertex) ||
			bufferData->vertexDesc.GetAttributeOffset(Vertex::VA_TEXCOORD0) != offsetof(RedsandRibbonVertex, u) ||
			bufferData->vertexDesc.GetAttributeOffset(Vertex::VA_COLOR) != offsetof(RedsandRibbonVertex, r)) { return false; }
		auto* device = reinterpret_cast<ID3D11Device*>(data->forwarder);
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(data->context);
		const auto inverse = shape.world.Invert();
		RE::NiPoint3 low{}, high{};
		{
			RedsandRibbonRenderLock lock(*renderer);
			auto* buffer = reinterpret_cast<ID3D11Buffer*>(bufferData->vertexBuffer);
			if (!buffer) { return false; }
			D3D11_BUFFER_DESC descriptor{};
			buffer->GetDesc(&descriptor);
			if (descriptor.ByteWidth != RedsandSwingTrail::Simulation::maxRibbonRows * 3 * sizeof(RedsandRibbonVertex) ||
				!(descriptor.BindFlags & D3D11_BIND_VERTEX_BUFFER)) { return false; }
			if (descriptor.Usage != D3D11_USAGE_DYNAMIC || !(descriptor.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE)) {
				descriptor.Usage = D3D11_USAGE_DYNAMIC;
				descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				ID3D11Buffer* writable = nullptr;
				if (FAILED(device->CreateBuffer(&descriptor, nullptr, &writable))) { return false; }
				bufferData->vertexBuffer = reinterpret_cast<REX::W32::ID3D11Buffer*>(writable);
				buffer->Release();
				buffer = writable;
			}
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return false; }
			auto* output = static_cast<RedsandRibbonVertex*>(mapped.pData);
			constexpr std::array<std::uint16_t, 3> u{0, 0x3800, 0x3c00}; // Half-float 0, .5, 1.
			const auto colorByte = [](float channel) {
				return static_cast<std::uint8_t>(std::round(255.f * std::clamp(channel, 0.f, 1.f)));
			};
			for (std::size_t i = 0; i < rows.size(); ++i) {
				const auto& row = rows[i];
				const std::array points{row.left, row.center, row.right};
				for (std::size_t j = 0; j < points.size(); ++j) {
					const auto& point = points[j];
					const auto local = inverse * RE::NiPoint3{point.x, point.y, point.z};
					RedsandRibbonVertex vertex{local};
					vertex.u = u[j];
					const float pale = j == 1 ? .85f * row.coreStrength : 0.f;
					vertex.r = colorByte(std::lerp(tint[0], 1.f, pale));
					vertex.g = colorByte(std::lerp(tint[1], 1.f, pale));
					vertex.b = colorByte(std::lerp(tint[2], 1.f, pale));
					vertex.a = colorByte(row.alpha);
					output[i * 3 + j] = vertex;
					if (i == 0 && j == 0) { low = high = local; }
					low.x = std::min(low.x, local.x); low.y = std::min(low.y, local.y); low.z = std::min(low.z, local.z);
					high.x = std::max(high.x, local.x); high.y = std::max(high.y, local.y); high.z = std::max(high.z, local.z);
				}
			}
			context->Unmap(buffer, 0);
			auto& draw = shape.GetTrishapeRuntimeData();
			draw.vertexCount = static_cast<std::uint16_t>(rows.size() * 3);
			draw.triangleCount = static_cast<std::uint16_t>((rows.size() - 1) * 4);
		}
		auto& bound = shape.GetModelData().modelBound;
		bound.center = (low + high) * .5f;
		bound.radius = (high - low).Length() * .5f + 1.f;
		shape.UpdateWorldBound();
		return true;
	}

	bool HasIndependentRedsandSwingData(RE::NiParticleSystem& system, RE::NiParticleSystem& source)
	{
		const auto* data = system.GetParticlesRuntimeData().particleData.get();
		const auto* original = source.GetParticlesRuntimeData().particleData.get();
		if (!data || !original || data == original ||
			system.GetGeometryRuntimeData().shaderProperty == source.GetGeometryRuntimeData().shaderProperty) { return false; }
		const auto& vertices = data->GetParticlesRuntimeData();
		const auto& shared = original->GetParticlesRuntimeData();
		return vertices.positions && vertices.positions != shared.positions && vertices.color && vertices.color != shared.color &&
			vertices.radii && vertices.radii != shared.radii && vertices.sizes && vertices.sizes != shared.sizes &&
			(!shared.rotationAngles || (vertices.rotationAngles && vertices.rotationAngles != shared.rotationAngles));
	}

	RE::NiPointer<RE::NiNode> CloneRedsandSwingTemplate()
	{
		const auto& prototype = RedsandSwingTemplate();
		auto* source = prototype ? FindRedsandSwingSystem(prototype.get()) : nullptr;
		if (!source) { return {}; }
		RE::NiPointer<RE::NiObject> clone(prototype->Clone());
		const auto independentRoot = [&]() -> RE::NiNode* {
			auto* root = clone && clone.get() != prototype.get() ? clone->AsNode() : nullptr;
			auto* system = root ? FindRedsandSwingSystem(root) : nullptr;
			return system && system != source && HasIndependentRedsandSwingData(*system, *source) ? root : nullptr;
		};
		auto* root = independentRoot();
		if (!root) {
			clone.reset();
			prototype->CreateDeepCopy(clone);
			root = independentRoot();
		}
		return RE::NiPointer<RE::NiNode>(root);
	}

	void PrepareRedsandSwingSystem(RE::NiAVObject& root, RE::NiParticleSystem& system)
	{
		RE::BSVisit::TraverseScenegraphObjects(&root, [](RE::NiAVObject* object) {
			for (auto controller = object->GetControllers(); controller; controller = controller->GetNext()) {
				controller->flags.reset(RE::NiTimeController::Flag::kActive);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		for (auto& modifier : system.GetParticleSystemRuntimeData().modifierList) {
			if (modifier) { modifier->SetActive(false); }
		}
		RE::BSVisit::TraverseScenegraphGeometries(&root, [&](RE::BSGeometry* geometry) {
			geometry->SetAppCulled(geometry != &system);
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		system.GetParticlesRuntimeData().particleData->GetParticlesRuntimeData().numVertices = 0;
	}

	template <class Emit>
	std::optional<unsigned> PublishRedsandSwingPoints(RE::NiParticleSystem& system, RE::NiAVObject& root, Emit emit)
	{
		auto* data = system.GetParticlesRuntimeData().particleData.get();
		if (!data || !std::isfinite(system.world.scale) || system.world.scale <= .0001f) { return std::nullopt; }
		auto& vertices = data->GetParticlesRuntimeData();
		if (!vertices.positions || !vertices.color || !vertices.radii || !vertices.sizes || !vertices.maxNumVertices) { return std::nullopt; }
		auto& runtime = data->GetPSysRuntimeData();
		const auto inverse = system.world.Invert();
		RE::NiPoint3 low{}, high{};
		float margin = 0.f;
		std::uint16_t count = 0;
		const auto write = [&](RedsandSwingTrail::Vec3 location, float radius, float intensity, float alpha, float age, float lifetime) {
			const auto i = count++;
			const auto position = inverse * RE::NiPoint3{location.x, location.y, location.z};
			vertices.positions[i] = position;
			vertices.color[i] = {intensity, intensity, intensity, alpha};
			vertices.radii[i] = radius / system.world.scale;
			vertices.sizes[i] = 1.f;
			margin = std::max(margin, vertices.radii[i]);
			if (vertices.rotationAngles) { vertices.rotationAngles[i] = 0.f; }
			if (vertices.rotationAxes) { vertices.rotationAxes[i] = {0.f, 0.f, 1.f}; }
			if (vertices.rotations) { vertices.rotations[i] = RE::NiQuaternion{1.f, 0.f, 0.f, 0.f}; }
			if (vertices.textureIndices) { vertices.textureIndices[i] = 0; }
			if (runtime.rotationSpeeds) { runtime.rotationSpeeds[i] = 0.f; }
			if (runtime.particleInfo) {
				runtime.particleInfo[i] = {};
				runtime.particleInfo[i].age = age;
				runtime.particleInfo[i].lifespan = lifetime;
			}
			if (!i) { low = high = position; }
			low.x = std::min(low.x, position.x); low.y = std::min(low.y, position.y); low.z = std::min(low.z, position.z);
			high.x = std::max(high.x, position.x); high.y = std::max(high.y, position.y); high.z = std::max(high.z, position.z);
		};
		emit(vertices.maxNumVertices, write);
		vertices.numVertices = count;
		auto& ps = system.GetParticleSystemRuntimeData();
		ps.isWorldspace = false; ps.dynamicBounds = false; ps.resetSystem = false;
		runtime.addedParticleCount = 0; runtime.addedParticlesBase = 0;
		vertices.bound.center = (low + high) * .5f;
		vertices.bound.radius = (high - low).Length() * .5f + margin + 1.f;
		system.GetModelData().modelBound = vertices.bound;
		system.UpdateWorldBound();
		unsigned depth = 0;
		for (auto* parent = system.parent; parent && depth < 32; parent = parent->parent, ++depth) {
			parent->UpdateWorldBound();
			if (parent == &root) { break; }
		}
		system.SetAppCulled(count == 0);
		return count;
	}
}

void AttackTrail::WarmRedsandSwingModel()
{
	(void)RedsandSwingTemplate();
}

std::size_t AttackTrail::UpdateRedsandSwingRibbon(RE::NiAVObject& root)
{
	auto* playerCamera = RE::PlayerCamera::GetSingleton();
	auto* camera = playerCamera ? playerCamera->cameraRoot.get() : nullptr;
	auto* parent = root.AsNode();
	if (!parent || !camera) {
		redsandRibbonRows.clear();
		redsandRibbonEnds.clear();
		if (redsandSwingRibbon) { redsandSwingRibbon->SetAppCulled(true); }
		return 0;
	}
	redsandSwingSimulation.BuildRibbon(SwingVector(camera->world.translate), redsandConfig.swordRibbonWidth,
		redsandRibbonRows, redsandRibbonEnds);
	if (redsandConfig.swordRibbonWidth <= 0.f || redsandConfig.redsand.arcBrightness <= 0.f ||
		std::ranges::none_of(redsandRibbonRows, [](const auto& row) { return row.alpha > 0.f; })) {
		if (redsandSwingRibbon) { redsandSwingRibbon->SetAppCulled(true); }
		return 0;
	}
	if (!redsandSwingRibbon) {
		redsandSwingRibbon = RedsandRibbonPool()[redsandSwingLease.Index()];
		if (!redsandSwingRibbon) {
			static std::atomic_bool reported = false;
			if (!reported.exchange(true)) { logger::warn("Redsand: connected ribbon model unavailable"); }
			return 0;
		}
		redsandSwingRibbon->local = RE::NiTransform{};
		parent->AttachChild(redsandSwingRibbon.get(), true);
	}
	auto& shape = *redsandSwingRibbon;
	RE::NiUpdateData update{};
	shape.Update(update);
	const auto& geometry = shape.GetGeometryRuntimeData();
	const auto& pool = RedsandRibbonPool();
	const bool privateShape = std::none_of(pool.begin(), pool.end(), [&](const auto& other) {
		if (!other || other.get() == &shape) { return false; }
		const auto& slot = other->GetGeometryRuntimeData();
		return (geometry.rendererData && geometry.rendererData == slot.rendererData) || geometry.shaderProperty == slot.shaderProperty;
	});
	auto* shader = netimmerse_cast<RE::BSEffectShaderProperty*>(geometry.shaderProperty.get());
	auto* material = shader ? shader->GetMaterial() : nullptr;
	const auto& custom = redsandConfig.redsand.color;
	const std::array rgb = custom.custom ? custom.rgb :
		std::array{redsandInheritedColor.red, redsandInheritedColor.green, redsandInheritedColor.blue};
	const float peak = std::max({rgb[0], rgb[1], rgb[2], 0.f});
	const std::array tint{rgb[0] / std::max(.001f, peak), rgb[1] / std::max(.001f, peak), rgb[2] / std::max(.001f, peak)};
	const float glow = peak > 0.f ? 2.6f * 5.4f * .85f * redsandConfig.redsand.arcBrightness : 0.f;
	const bool ready = privateShape && RedsandMaterialColor::Commit(shader, material, {1.f, 1.f, 1.f}, glow, false,
		[](auto* copy) { copy->~BSEffectShaderMaterial(); RE::free(copy); }) && WriteRedsandRibbon(shape, redsandRibbonRows, tint);
	if (!ready) {
		static std::atomic_bool reported = false;
		if (!reported.exchange(true)) {
			RedsandLog::info("Redsand ribbon vertex upload unavailable: private={} renderer={} shader={}",
				privateShape, geometry.rendererData != nullptr, shader != nullptr);
		}
	}
	shape.SetAppCulled(!ready);
	return ready ? redsandRibbonRows.size() : 0;
}

void AttackTrail::RetireRedsandSwingTrail()
{
	if (redsandSwingRibbon) {
		redsandSwingRibbon->SetAppCulled(true);
		if (auto* parent = redsandSwingRibbon->parent) { parent->DetachChild(redsandSwingRibbon.get()); }
		redsandSwingRibbon.reset();
	}
	if (redsandSwingRoot) {
		redsandSwingRoot->SetAppCulled(true);
		if (auto* parent = redsandSwingRoot->parent) { parent->DetachChild(redsandSwingRoot.get()); }
		redsandSwingRoot.reset();
	}
	redsandSwingActorRoot.reset();
	if (trailParticle) {
		if (auto root = trailParticle->particleObject) { root->SetAppCulled(true); }
		trailParticle->age = trailParticle->lifetime + 1.f;
	}
	bActive = false;
}

bool AttackTrail::CreateRedsandSwingRoot(RE::Actor& actor)
{
	auto* actorRoot = actor.Get3D();
	if (!actorRoot || !actorRoot->AsNode()) { return false; }
	redsandSwingRoot = CloneRedsandSwingTemplate();
	if (!redsandSwingRoot) { return false; }
	auto* root = redsandSwingRoot.get();
	redsandSwingActorRoot.reset(actorRoot);
	root->SetAppCulled(true);
	root->local = RE::NiTransform{};
	actorRoot->AsNode()->AttachChild(root, true);
	return true;
}

bool AttackTrail::UpdateRedsandSwingTrail(float delta)
{
	if (!std::isfinite(delta) || delta <= 0.f) { return bActive; }
	const auto retire = [&]() {
		RetireRedsandSwingTrail();
		return false;
	};
	auto actor = actorHandle.get();
	auto* actorRoot = actor ? actor->Get3D() : nullptr;
	redsandConfig = RedsandSettings::Get();
	RedsandSettings::ApplyMeleeAppearance(redsandConfig, redsandEnchantmentKey, redsandEnchantmentFallback);
	if (!bActive || !redsandSwingLease.Acquire()) { return retire(); }
	const bool scope = actorRoot && Settings::bDisplayTrails && Settings::bAttackCollisionsEnabled &&
		!Settings::bDisableMod && (actor->IsPlayerRef() ? redsandConfig.redsand.player : redsandConfig.redsand.npc) &&
		(!redsandSwingActorRoot || actorRoot == redsandSwingActorRoot.get());
	if (!bExpired) {
		bExpired = !scope || !redsandConfig.swordReleasedTrail || !collisionParentNode || !collisionNode ||
			!PrecisionHandler::GetSingleton()->GetAttackCollision(actorHandle, collisionNode.get());
	}
	const bool emitting = !bExpired;
	redsandSwingSimulation.AdvanceFrame(delta);
	std::optional<RedsandSwingTrail::Vec3> measuredTip;
	if (emitting) {
		const auto span=Redsand::Weapons::MeleeEmissionSpan(redsandWeaponProfile);
		const float fallbackReach = std::abs(scale) * 100.f * span.end;
		if (!redsandBladeMeasured) {
			redsandBladeMeasured = true;
			RedsandSwingTrail::BladeTip vertices, bounds;
			unsigned remaining = 32768;
			for (auto& child : collisionParentNode->children) {
				if (child && child.get() != collisionNode.get()) {
					GatherRedsandBladeTip(*child, RE::NiTransform{}, 12, vertices, bounds, remaining);
				}
			}
			const auto& selected = vertices.reachSquared > 0.f ? vertices : bounds;
			if (selected.reachSquared > 0.f) {
				redsandBladeTip = RE::NiPoint3{selected.point.x, selected.point.y, selected.point.z};
				redsandBladeSource = vertices.reachSquared > 0.f ? "mesh-vertices" : "mesh-bounds";
			}
		}
		const auto pose = [&](const RE::NiTransform& transform) {
			const auto& base = transform.translate;
			const auto tip = redsandBladeTip ? transform * *redsandBladeTip :
				base + transform.rotate * RE::NiPoint3{0.f, fallbackReach, 0.f};
			return RedsandSwingTrail::Pose{SwingVector(base), SwingVector(tip), SwingVector(actor->GetPosition())};
		};
		if (redsandSwingStart) {
			redsandSwingSimulation.Seed(pose(*redsandSwingStart));
			redsandSwingStart.reset();
		}
		const auto current = pose(collisionParentNode->world);
		measuredTip = current.tip;
		const auto timing = RedsandSettings::SwingTiming::FromDuration(RedsandSettings::SwingDuration(redsandConfig));
		const RedsandSwingTrail::Parameters parameters{timing.arcHold,timing.particleHold,
			redsandConfig.swordRibbonWidth,redsandConfig.swordTrailMomentum,redsandConfig.redsand.density,redsandConfig.redsand.size,
			timing.arcFade,timing.particleFade,redsandConfig.swordParticleWidth};
		redsandSwingSimulation.Sample(current.base, current.tip, delta, parameters, current.actor);
	}
	if (!emitting && redsandSwingSimulation.EndAttack() && redsandActorIsPlayer && redsandConfig.diagnostics) {
		float arc = 0.f;
		float grains = 0.f;
		for (const auto& particle : redsandSwingSimulation.Particles()) {
			auto& remaining = particle.ribbon ? arc : grains;
			remaining = std::max(remaining, particle.lifetime - particle.age);
		}
		unsigned rounded = 0, detours = 0;
		for (const auto& ends : redsandRibbonEnds) { rounded += ends.rounded; detours += ends.detours; }
		RedsandLog::info("Redsand arc emission ended: particles={} arcRemaining={} grainsRemaining={} roundedEnds={} localDetours={}",
			redsandSwingSimulation.Particles().size(), arc, grains, rounded, detours);
	}
	if (redsandSwingSimulation.Empty() && !redsandSwingSpawnAttempted) { return emitting ? true : retire(); }
	if (!emitting && redsandSwingSimulation.Empty()) { return retire(); }

	if (!redsandSwingSpawnAttempted) {
		redsandSwingSpawnAttempted = true;
		if (!CreateRedsandSwingRoot(*actor)) {
			if (auto* cell = actor->GetParentCell()) {
				const auto& transform = collisionParentNode->world;
				trailParticle.reset(RE::BSTempEffectParticle::Spawn(
					cell, 10.f, redsandSwingMesh, transform.rotate, transform.translate, 1.f, 7, nullptr));
			}
		}
	}
	RE::NiAVObject* root = redsandSwingRoot.get();
	if (!root && trailParticle) { root = trailParticle->particleObject.get(); }
	const auto wait = [&]() {
		redsandSwingWaiting += delta;
		if (!redsandSwingPublished) { redsandSwingSimulation.DiscardParticles(); }
		return emitting && (redsandSwingRoot || trailParticle) && redsandSwingWaiting < .15f ? true : retire();
	};
	if (!root) { return wait(); }
	if (!redsandChecked) {
		if (auto* system = FindRedsandSwingSystem(root)) { redsandSystems.emplace_back(system); }
		redsandChecked = true;
	}
	if (redsandSystems.empty()) { return retire(); }
	auto* system=redsandSystems.front().get();
	auto* data=system->GetParticlesRuntimeData().particleData.get();
	if (!data) { return wait(); }
	auto& vertices=data->GetParticlesRuntimeData();
	if (!vertices.positions || !vertices.color || !vertices.radii || !vertices.sizes || !vertices.maxNumVertices) { return retire(); }
	if (!redsandSwingReady) {
		PrepareRedsandSwingSystem(*root, *system);
		redsandSwingReady = true;
	}
	if (redsandSwingRoot) {
		RE::NiUpdateData update{};
		root->Update(update);
	}
	if (!std::isfinite(system->world.scale) || system->world.scale <= .0001f) { return retire(); }
	redsandSwingWaiting=0.f;
	redsandSwingSimulation.SetRenderCapacity(vertices.maxNumVertices);
	const auto& particles=redsandSwingSimulation.Particles();
	if (!bAppliedTrailColorSettings) {
		redsandInheritedColor = baseColorOverride.value_or(redsandInheritedColor);
	}
	const auto arcRows = UpdateRedsandSwingRibbon(*root);
	const auto& style = redsandConfig.redsand;
	const bool drawFallback = !arcRows && redsandConfig.swordRibbonWidth > 0.f;
	const auto lighting = RedsandSwingTrail::ResolvePointLighting(style.particleBrightness, style.arcBrightness, drawFallback);
	redsandPointMaterialBrightness = lighting.materialBrightness;
	ApplyColorSettings(system, !bAppliedTrailColorSettings, false);
	bAppliedTrailColorSettings = true;
	unsigned grains = 0;
	unsigned fallback = 0;
	PublishRedsandSwingPoints(*system, *root, [&](unsigned capacity, const auto& write) {
		if (lighting.grainOpacity > 0.f) {
			for (const auto& particle : particles) {
				if (particle.ribbon) { continue; }
				if (grains == capacity) { break; }
				const auto placement = redsandSwingSimulation.PlaceScatterOnRibbon(particle, redsandRibbonRows, redsandRibbonEnds);
				if (placement.opacity <= 0.f) { continue; }
				write(placement.position, particle.radius, particle.intensity,
					particle.Alpha() * placement.opacity * lighting.grainOpacity, particle.age, particle.lifetime);
				++grains;
			}
		}
		if (lighting.fallbackOpacity > 0.f) {
			fallback = RedsandSwingTrail::SampleFallbackRibbon(redsandRibbonRows, std::min(2048u, capacity - grains),
				[&](auto position, float radius, float alpha) { write(position, radius, .85f, alpha * lighting.fallbackOpacity, 0.f, 1.f); });
		}
	});
	root->SetAppCulled(false);
	if (auto* fade=root->AsFadeNode()) { fade->currentFade=1.f; }
	if (trailParticle) { trailParticle->lifetime=std::max(trailParticle->lifetime,trailParticle->age+1.f); }
	if ((arcRows || fallback || grains) && !redsandSwingPublished) {
		redsandSwingPublished = true;
		if (redsandActorIsPlayer && redsandConfig.diagnostics) {
			const auto tip = redsandBladeTip.value_or(RE::NiPoint3{});
			const float gap = measuredTip && !redsandRibbonRows.empty() ? (redsandRibbonRows.back().center - *measuredTip).Length() : -1.f;
			RedsandLog::info("Redsand arc first frame: window=collision renderer={} ribbonRows={} fallbackPoints={} grains={} grainPlacement=centerline-settle grainShape=round dotDensity={} speed={} duration={} source={} localTip={}/{}/{} fit=pose-trace-soft-corners tipGap={:.3f}",
				redsandSwingRoot ? "attached" : "temporary", arcRows, fallback, grains, style.density,
				redsandConfig.swordTrailMomentum, RedsandSettings::SwingDuration(redsandConfig), redsandBladeSource, tip.x, tip.y, tip.z, gap);
		}
	}
	return true;
}
