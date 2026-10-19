#pragma once
#include "time_session.hpp"
#include <array>
#include <span>
#include <vector>



namespace HellbladeTime::Lightning {
struct Vec {
    float x=0,y=0;
    Vec operator+(Vec b) const { return {x+b.x,y+b.y}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y}; }
    Vec operator*(float b) const { return {x*b,y*b}; }
};
inline float Length(Vec v) { return std::sqrt(v.x*v.x+v.y*v.y); }
inline Vec Unit(Vec v) { const auto len=Length(v); return len>.00001f ? v*(1/len) : Vec{1,0}; }
inline float Smooth(float v) { v=std::clamp(v,0.f,1.f);return v*v*(3-2*v); }
inline std::uint32_t Hash(std::uint32_t x) { x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16); }
inline float Random(std::uint32_t seed) {return float(Hash(seed)>>8)*(1.f/16777216.f);}
inline float Noise(float x,std::uint32_t seed) {
    const auto i=static_cast<std::int32_t>(std::floor(x));
    return std::lerp(Random(seed+static_cast<std::uint32_t>(i)),
        Random(seed+static_cast<std::uint32_t>(i+1)),Smooth(x-float(i)))*2-1;
}
struct Vertex { float x,y,u,v,red,green,blue,energy; };
inline constexpr std::size_t MaxVertices=131072;
struct Geometry {
    std::vector<Vertex> vertices;
    unsigned paths=0;
    Geometry() {vertices.reserve(MaxVertices);}
    void Strip(std::span<const Vec> points,float width,float energy,const VisualOptions& opt,
        float head=1.f,float tail=0.f) {
        if(points.size()<2 || energy<.0001f) return;
        const auto count=points.size();
        std::array<Vertex,130> strip;
        if(count>65 || vertices.size()+(count-1)*6>MaxVertices) return;
        for(std::size_t i=0;i<count;++i) {
            const float s=float(i)/float(count-1);


            const float visibility=Smooth((head-s)*16)*Smooth((s-tail)*12+.8f);
            auto tangent=Unit(points[std::min(i+1,count-1)]-points[i ? i-1 : 0]);
            Vec n={-tangent.y,tangent.x};
            const float taper=std::max(.32f,1-s*.6f);
            const float halfWidth=width*taper;
            for(unsigned j=0;j<2;++j) {
                auto p=points[i]+n*(j ? halfWidth : -halfWidth);
                strip[i*2+j]={p.x,p.y,j ? 1.f : -1.f,s,opt.red,opt.green,opt.blue,energy*visibility};
            }
        }
        for(std::size_t i=1;i<count;++i) {
            const auto a=(i-1)*2,b=i*2;
            for(auto k : {a,a+1,b,b,a+1,b+1}) vertices.push_back(strip[k]);
        }
        ++paths;
    }
    void Path(std::span<Vec> result,Vec start,Vec end,float time,std::uint32_t seed,float roughness) {
        const auto d=end-start;
        const auto tangent=Unit(d); const Vec normal={-tangent.y,tangent.x};
        const float len=Length(d);
        for(std::size_t i=0;i<result.size();++i) {
            const float s=float(i)/float(result.size()-1);
            const float pin=std::sin(s*3.14159265f);


            const float bend=(Noise(s*4+time*.8f,seed)*.65f+
                Noise(s*17-time*2.1f,seed+173)*.24f+
                Noise(s*51+time*3.2f,seed+971)*.11f)*len*roughness*pin;
            result[i]=start+d*s+normal*bend;
        }
    }
    void Build(const VisualFrame& frame,float aspect) {
        vertices.clear(); paths=0;
        const auto& session=frame.session; const auto& opt=frame.options;
        if(!session.active || !opt.enabled || !std::isfinite(session.elapsed) ||
            !std::isfinite(session.duration) || session.duration<=0 || session.elapsed<0 ||
            session.elapsed>=session.duration || !std::isfinite(aspect) || aspect<=0 || opt.brightness<=0) return;
        const float t=session.elapsed,duration=session.duration;
        const float envelope=Smooth(t/.07f)*(1-Smooth((t-duration*.65f)/std::max(.04f,duration*.35f)));
        if(envelope<=0) return;
        const float density=FiniteClamp(opt.density,1,.25f,3);
        const int rays=std::clamp(int(9*density),3,27);
        const float reach=std::sqrt(aspect*aspect+1.f)*1.1f;
        const auto key=Hash(session.serial+0x48425431);
        const int cycle=int(t/.62f);



        for(int generation=std::max(0,cycle-1);generation<=cycle;++generation) {
            const float age=t-float(generation)*.62f;
            for(int i=0;i<rays;++i) {
                const auto seed=Hash(key+std::uint32_t(generation)*1399+std::uint32_t(i)*1777);
                const float birth=Random(seed+1)*.12f;
                const float local=age-birth;
                if(local<0 || local>1.1f) continue;
                const float life=Smooth(local/.035f)*(1-Smooth((local-.36f)/.68f));
                const float angle=float(i)*6.2831853f/float(rays)+Random(key)*6.28f+
                    (Random(seed+2)-.5f)*.9f+float(generation)*.37f;
                const Vec radial={std::cos(angle),std::sin(angle)};
                const Vec tangent={-radial.y,radial.x};
                const float inner=generation==0 ? .13f+Random(seed+5)*.16f : .45f+Random(seed+5)*.28f;
                Vec start=radial*(inner+local*.12f)+Vec{0,.04f};
                Vec end=radial*(reach*(.7f+Random(seed+3)*.35f))+tangent*(Noise(t*.8f,seed+65)*.15f);
                std::array<Vec,65> trunk;
                Path(trunk,start,end,t,seed,.16f);
                const float energy=envelope*life*(.55f+Random(seed+8)*.4f)*opt.brightness;
                const float head=local/.28f;
                const float tail=std::max(0.f,(local-.42f)*.72f);
                Strip(trunk,.025f,energy,opt,head,tail);

                for(unsigned filament=0;filament<2;++filament) {
                    std::array<Vec,33> silk;
                    const unsigned begin=filament*21+3;
                    for(unsigned k=0;k<silk.size();++k) {
                        const auto index=std::min(begin+k,64u);
                        const float offset=std::sin(float(k)*3.14159265f/32)*
                            (.012f+Noise(float(k)*.23f-t*1.3f,seed+filament*19)*.009f);
                        silk[k]=trunk[index]+tangent*(offset*(filament ? -1.f : 1.f));
                    }
                    Strip(silk,.012f,energy*.32f,opt,(head-float(begin)/64)*2,tail);
                }
                for(unsigned b=0;b<4;++b) {
                    const auto branchSeed=Hash(seed+b*229+97);
                    const auto at=std::size_t(10+b*11+int(Random(branchSeed)*7));
                    const float branchStart=float(at)/64;
                    const float branchAge=local-branchStart*.28f;
                    if(branchAge<=0) continue;
                    const float side=b%2 ? -1.f : 1.f;
                    const Vec tip=trunk[at]+radial*(.2f+Random(branchSeed+1)*.4f)+
                        tangent*(side*(.08f+Random(branchSeed+2)*.3f));
                    std::array<Vec,33> branch;
                    Path(branch,trunk[at],tip,t,branchSeed,.14f);
                    Strip(branch,.015f,energy*.55f,opt,branchAge/.17f,tail);

                    std::array<Vec,17> twig;
                    Path(twig,branch[18],branch[18]+tangent*(side*.11f)+radial*.18f,t,branchSeed+37,.16f);
                    Strip(twig,.009f,energy*.3f,opt,(branchAge-.1f)/.1f,tail);
                    std::array<Vec,9> fine;
                    Path(fine,twig[9],twig[9]+radial*.08f-tangent*(side*.055f),t,branchSeed+61,.17f);
                    Strip(fine,.006f,energy*.15f,opt,(branchAge-.15f)/.08f,tail);
                }
            }
        }


        for(int i=0;i<int(11*density);++i) {
            const auto seed=Hash(key+std::uint32_t(i)*821);
            const float age=t-Random(seed)*.16f;
            if(age<0 || age>1.15f) continue;
            const float radius=.24f+age*1.6f;
            const float angle=Random(seed+2)*6.2831853f+age*.16f;
            std::array<Vec,33> arc;
            for(unsigned j=0;j<arc.size();++j) {
                const float s=float(j)/32;
                const float a=angle+(s-.5f)*(.18f+Random(seed+3)*.28f);
                const float r=radius+Noise(s*21+t*2,seed+5)*.018f;
                arc[j]={std::cos(a)*r,std::sin(a)*r};
            }
            Strip(arc,.013f,envelope*.5f*(1-Smooth((age-.35f)/.8f))*opt.brightness,opt);
        }

        for(int i=0;i<int(52*density);++i) {
            const auto seed=Hash(key+std::uint32_t(i)*3001);
            const float age=t-Random(seed+1)*.85f;
            if(age<0 || age>.7f) continue;
            const float angle=Random(seed+2)*6.2831853f;
            const Vec radial={std::cos(angle),std::sin(angle)},tangent={-radial.y,radial.x};
            const Vec origin=radial*(.3f+Random(seed+3)*.6f+age*(.3f+Random(seed+4)));
            std::array<Vec,5> mote;
            const float len=.015f+Random(seed+5)*.05f;
            for(unsigned k=0;k<mote.size();++k) {
                const float s=float(k)/4;
                mote[k]=origin+radial*(s*len)+tangent*(Noise(s*3+t,seed+9)*len*.28f);
            }
            Strip(mote,.007f,envelope*Smooth(age/.04f)*(1-Smooth(age/.7f))*.45f*opt.brightness,opt);
        }
    }
};
}
