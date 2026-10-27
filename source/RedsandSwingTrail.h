#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace RedsandSwingTrail
{
	struct Vec3
	{
		float x = 0.f, y = 0.f, z = 0.f;
		Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
		Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
		Vec3 operator*(float n) const { return {x*n,y*n,z*n}; }
		float Dot(Vec3 b) const { return x*b.x+y*b.y+z*b.z; }
		float Length() const { return std::sqrt(Dot(*this)); }
		bool Finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
	};
	struct Parameters
	{
		float ribbonLife = .2f, particleLife = .3f, width = .5f, momentum = .1f;
		float density = 1.f, size = 1.f, spread = 1.f;
		float ribbonFade = .25f, particleFade = .4f, gravity = .3f;
	};
	struct Particle
	{
		Vec3 position, velocity;
		float age = 0.f, lifetime = .1f, radius = 1.f, intensity = 1.f;
		bool ribbon = false;
		float hold = 0.f;
		float fadeStart = 0.f, gravity = 0.f;
		float Fade() const { const float u=std::clamp((age-fadeStart)/std::max(.001f,lifetime-fadeStart),0.f,1.f);return u*u*(3.f-2.f*u); }
		float Alpha() const { return 1.f-Fade(); }
		float Size() const { return ribbon ? radius*(1.f-.45f*Fade()) : radius; }
	};
	struct Pose { Vec3 base, tip; };
	struct Tangent { Vec3 base, tip;float length=0.f; };
	struct Curve
	{
		Pose start, end;
		Tangent first, last;
		float duration;
		float startLength,endLength;
		Curve(Pose a,Pose b,Tangent va,Tangent vb,float dt) : start(a),end(b),first(va),last(vb),duration(dt),
			startLength((a.tip-a.base).Length()),endLength((b.tip-b.base).Length()) {}
		static Vec3 Hermite(Vec3 a,Vec3 b,Vec3 va,Vec3 vb,float t,float dt)
		{
			return a*(2*t*t*t-3*t*t+1)+va*((t*t*t-2*t*t+t)*dt)+b*(-2*t*t*t+3*t*t)+vb*((t*t*t-t*t)*dt);
		}
		Pose At(float t) const
		{
			const auto base=Hermite(start.base,end.base,first.base,last.base,t,duration);
			const auto tip=Hermite(start.tip,end.tip,first.tip,last.tip,t,duration);
			const auto axis=tip-base;const float rawLength=axis.Length();
			const float length=Hermite({startLength,0,0},{endLength,0,0},{first.length,0,0},{last.length,0,0},t,duration).x;
			return {base,base+axis*(std::max(.1f,length)/std::max(.0001f,rawLength))};
		}
		Vec3 Position(float t,float along) const { const auto p=At(t);return p.base+(p.tip-p.base)*along; }
		Vec3 Velocity(float t,float along) const
		{
			const float lo=std::max(0.f,t-.001f),hi=std::min(1.f,t+.001f);
			return (Position(hi,along)-Position(lo,along))*(1.f/((hi-lo)*duration));
		}
		struct Evaluation
		{
			Pose at,before,after;
			float inverseTime;
			static Vec3 Along(Pose p,float along) { return p.base+(p.tip-p.base)*along; }
			Vec3 Position(float along) const { return Along(at,along); }
			Vec3 Velocity(float along) const { return (Along(after,along)-Along(before,along))*inverseTime; }
		};
		Evaluation Evaluate(float t) const
		{
			const float lo=std::max(0.f,t-.001f),hi=std::min(1.f,t+.001f);
			return {At(t),At(lo),At(hi),1.f/((hi-lo)*duration)};
		}
	};
	class Lease
	{
	public:
		static constexpr unsigned maximum = 32;
		Lease() = default;
		Lease(const Lease&) = delete;
		Lease& operator=(const Lease&) = delete;
		~Lease() { if (held) { count.fetch_sub(1); } }
		bool Acquire()
		{
			if (held) { return true; }
			auto current = count.load();
			while (current < maximum) { if (count.compare_exchange_weak(current,current+1)) { held = true; return true; } }
			return false;
		}
	private:
		bool held = false;
		inline static std::atomic<unsigned> count{0};
	};
	class Simulation
	{
	public:
		static constexpr std::size_t capacity = 4096;
		static constexpr unsigned maxSamplesPerFrame = 256;
		const std::vector<Particle>& Particles() const { return particles; }
		bool Empty() const { return particles.empty(); }
		void Break()
		{
			for (auto& p:particles) { p.hold=0.f; }
			sampled=hasTangent=false;remainder=idle=0.f;
		}
		void Advance(float delta)
		{
			if (!std::isfinite(delta) || delta <= 0.f) { return; }
			const auto arc=Motion::For(delta,9.f),grain=Motion::For(delta,3.f);
			for (auto& p : particles) { Move(p,delta,p.ribbon ? arc : grain); }
			std::erase_if(particles,[](const auto& p) { return p.age >= p.lifetime; });
		}
		void Sample(Vec3 base, Vec3 tip, float delta, const Parameters& parameters)
		{
			if (!base.Finite() || !tip.Finite() || !std::isfinite(delta) || delta <= 0.f || delta > .1f) { Break(); return; }
			const Pose next{base,tip};const float reach=(tip-base).Length();
			if (reach<.1f || reach>500.f) { Break();return; }
			if (!sampled) { current=next;sampled=true;return; }
			const float distance=(tip-current.tip).Length();
			const auto oldAxis=current.tip-current.base,newAxis=tip-base;
			if (distance>160.f || (base-current.base).Length()>120.f || oldAxis.Dot(newAxis)<-.9f*oldAxis.Length()*reach) { Break();return; }
			if (distance<.15f) { idle+=delta;if (idle>=.04f) { Break(); }return; }
			const float elapsed=delta+idle;idle=0.f;
			const auto secant=Slope(current,next,elapsed);
			Tangent first=secant,last=secant;
			if (hasTangent && (tangent.base.Length()*elapsed>120.f || tangent.tip.Length()*elapsed>160.f)) {hasTangent=false;}
			if (hasTangent) {
				const auto before=Slope(previous,current,interval),after=Slope(current,next,elapsed);
				const float weight=elapsed/(interval+elapsed);
				const auto limited=[](Vec3 value,Vec3 slope){const float limit=2.f*slope.Length(),length=value.Length();return length>limit && length>0.f ? value*(limit/length) : value;};
				first=tangent;
				last={limited(after.base+(after.base-before.base)*weight,after.base),limited(after.tip+(after.tip-before.tip)*weight,after.tip),
					std::clamp(after.length+(after.length-before.length)*weight,-2.f*std::abs(after.length),2.f*std::abs(after.length))};
			}
			Emit({current,next,first,last,elapsed},0.f,parameters);
			previous=current;current=next;interval=elapsed;tangent=last;hasTangent=true;
		}
	private:
		static Tangent Slope(Pose a,Pose b,float delta) { return {(b.base-a.base)*(1.f/delta),(b.tip-a.tip)*(1.f/delta),((b.tip-b.base).Length()-(a.tip-a.base).Length())/delta}; }
		void Emit(const Curve& curve,float delay,const Parameters& parameters)
		{
			if (!particles.capacity()) { particles.reserve(1024); }
			float distance=0.f;auto prior=curve.Position(0.f,1.f);
			for (unsigned i=1;i<=8;++i) { const auto p=curve.Position(i/8.f,1.f);distance+=(p-prior).Length();prior=p; }
			const unsigned samples=std::min(maxSamplesPerFrame,std::max(1u,static_cast<unsigned>(std::ceil(distance/.5f))));
			for (unsigned i=0;i<samples;++i) {
				const float t=(i+.5f)/samples;
				const auto evaluated=curve.Evaluate(t);
				const float birthAge=delay+curve.duration*(1.f-t);
				const auto arcMotion=Motion::For(birthAge,9.f);
				const auto velocityAt=[&](float along) {
					Vec3 velocity=evaluated.Velocity(along)*parameters.momentum;
					const float speed=velocity.Length();return speed>80.f ? velocity*(80.f/speed) : velocity;
				};
				for (unsigned strand=0;strand<3;++strand) {
					const float along = .88f+strand*.06f;
					Particle p{evaluated.Position(along),velocityAt(along),0.f,parameters.ribbonLife+parameters.ribbonFade,
						parameters.width*(strand==1 ? 1.15f : .45f),strand==1 ? .85f : .5f,true,.24f,parameters.ribbonLife,0.f};
					Move(p,birthAge,arcMotion); Add(p);
				}
				remainder += distance/samples*.6f*parameters.density;
				const unsigned grains=std::min(12u,static_cast<unsigned>(remainder)); remainder-=grains;
				const auto grainMotion=grains ? Motion::For(birthAge,3.f) : Motion{};
				for (unsigned grain=0;grain<grains;++grain) {
					const float along=.87f+Random()*.145f;
					const float jitter=3.f*parameters.spread;
					const Vec3 offset{(Random()-.5f)*jitter,(Random()-.5f)*jitter,(Random()-.5f)*jitter};
					const auto velocity=velocityAt(along)+offset*2.f;
					const float retained=std::max(parameters.ribbonLife+.02f,parameters.particleLife*(.85f+Random()*.3f));
					const float fade=std::max(parameters.particleFade,parameters.ribbonLife+parameters.ribbonFade+.04f-retained);
					Particle p{evaluated.Position(along)+offset,velocity,0.f,retained+fade,
						(.45f+Random()*.3f)*parameters.size,.8f+Random()*.2f,false,.24f,retained,-240.f*parameters.gravity};
					Move(p,birthAge,grainMotion); Add(p);
				}
			}
		}
		std::vector<Particle> particles;
		Pose previous,current;
		Tangent tangent;
		bool sampled = false,hasTangent=false;
		float remainder = 0.f,interval=0.f,idle=0.f;
		std::uint32_t random = 0x912e5c3bu;
		float Random() { random^=random<<13;random^=random>>17;random^=random<<5;return (random&0xffffff)/16777216.f; }
		struct Motion
		{
			float decay=1.f,travel=0.f,fall=0.f;
			static Motion For(float delta,float drag) { const float decay=std::exp(-drag*delta),travel=(1.f-decay)/drag;return {decay,travel,(delta-travel)/drag}; }
		};
		static void Move(Particle& p,float delta,Motion motion)
		{
			p.position=p.position+p.velocity*motion.travel;p.velocity=p.velocity*motion.decay;
			p.position.z+=p.gravity*motion.fall;p.velocity.z+=p.gravity*motion.travel;
			const float held=std::min(p.hold,delta);p.hold-=held;p.age+=delta-held;
		}
		void Add(const Particle& p) { if (p.age<p.lifetime && particles.size()<capacity) { particles.push_back(p); } }
	};
}
