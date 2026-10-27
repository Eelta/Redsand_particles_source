#include "../source/RedsandSwingTrail.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

void Check(bool value,const char* message) { if (!value) { throw std::runtime_error(message); } }
int main() try
{
    using namespace RedsandSwingTrail;
    Parameters parameters;parameters.ribbonLife=.08f;parameters.particleLife=.16f;parameters.width=1.f;parameters.ribbonFade=.18f;parameters.particleFade=.28f;parameters.momentum=0.f;parameters.gravity=0.f;
    Simulation simulation;
    const auto pose=[](float angle) { return Pose{{0,0,0},{std::sin(angle)*100.f,std::cos(angle)*100.f,0}}; };
    const auto sample=[&](Simulation& sim,float angle,float dt) { const auto p=pose(angle);sim.Sample(p.base,p.tip,dt,parameters); };
    sample(simulation,0.f,.016f);sample(simulation,.4f,.016f);
    Check(!simulation.Empty(),"current movement waited for a future frame");
    float nearest=1000.f;for(const auto& p:simulation.Particles())if(p.ribbon && p.intensity==.85f)nearest=std::min(nearest,(p.position-pose(.4f).tip*.94f).Length());
    Check(nearest<.75f,"visible arc head trails behind the current weapon pose");
    sample(simulation,.8f,.016f);
    bool core=false,grains=false;
    for (const auto& p:simulation.Particles()) {
        core|=p.ribbon;grains|=!p.ribbon;
        if (!p.ribbon) {
            Check(p.radius>=.45f && p.radius<=.75f,"fine particles regressed to invisible subpixel radii");
            Check(p.lifetime>parameters.ribbonLife+parameters.ribbonFade,"residual particle expired before the arc");
        }
    }
    Check(core && grains,"arc and residual grains were not emitted together");
    Particle arc;arc.ribbon=true;arc.lifetime=.24f;arc.fadeStart=.06f;arc.radius=1.f;arc.age=.05f;
    Check(arc.Alpha()==1.f && arc.Size()==1.f,"arc did not retain its configured hold");
    arc.age=.15f;
    Check(std::abs(arc.Alpha()-.5f)<.0001f && arc.Size()>.77f,"arc did not fade smoothly or shrank out of sight early");
    float priorAlpha=1.f;
    for(unsigned i=0;i<=24;++i) {arc.age=i*.01f;const float alpha=arc.Alpha();Check(alpha<=priorAlpha && priorAlpha-alpha<.09f,"arc opacity changes abruptly");priorAlpha=alpha;}
    Check(arc.Alpha()==0.f,"arc ended before becoming transparent");
    Particle point;point.fadeStart=.2f;point.lifetime=.48f;point.age=.34f;
    Check(std::abs(point.Alpha()-.5f)<.0001f,"residual particle still fades only during its final few frames");
    const auto initial=simulation.Particles().front().position;
    for(unsigned i=0;i<9;++i) {simulation.Advance(.016f);sample(simulation,.9f+i*.1f,.016f);}
    Check((simulation.Particles().front().position-initial).Length()<.0001f,"retained arc followed later weapon motion");
    Check(simulation.Particles().front().age==0.f,"front of the arc disappeared before the swing ended");
    const auto beforeFlush=simulation.Particles().size();simulation.Break();
    Check(simulation.Particles().size()==beforeFlush,"swing end emitted a stale buffered segment");
    simulation.Advance(.04f);
    for(const auto& p:simulation.Particles()) { if(p.ribbon) { Check(p.Size()>p.radius*.49f,"arc did not retain its useful post-swing width"); } }
    simulation.Advance(.041f);
    bool lingeringArc=false;for(const auto& p:simulation.Particles()) {lingeringArc|=p.ribbon;}
    Check(lingeringArc,"arc vanished after its old short retention instead of using the separate fade");
    simulation.Advance(.18f);
    Check(!simulation.Empty(),"particles disappeared with the ribbon");
    for(const auto& p:simulation.Particles()) {Check(!p.ribbon,"arc exceeded its configured hold and fade time");}
    const auto residual=simulation.Particles().size();simulation.Advance(.18f);
    Check(simulation.Particles().size()<residual,"residual points did not thin as they expired");
    simulation.Advance(.2f);Check(simulation.Empty(),"residual particles leaked after expiry");

    Simulation curved;
    for(unsigned i=0;i<7;++i) {sample(curved,i*.35f,.033f);}
    curved.Break();std::vector<Vec3> middle;
    for(const auto& p:curved.Particles()) {
        if(p.ribbon && p.intensity==.85f) {
            Check(std::abs(p.position.Length()-94.f)<.03f,"curve collapsed inward into endpoint chords");middle.push_back(p.position);
        }
    }
    for(std::size_t i=2;i<middle.size();++i) {
        const auto a=middle[i-1]-middle[i-2],b=middle[i]-middle[i-1];
        Check(a.Dot(b)/(a.Length()*b.Length())>.995f,"multi-frame circular arc has a visible corner");
    }
    const Pose a{{0,0,0},{0,100,0}},b{{2,1,0},{52,87.6025f,0}},c{{5,3,0},{91.6025f,53,0}};
    const float dt1=.02f,dt2=.04f;
    const Tangent ab{(b.base-a.base)*(1.f/dt1),(b.tip-a.tip)*(1.f/dt1)};
    const Tangent bc{(c.base-b.base)*(1.f/dt2),(c.tip-b.tip)*(1.f/dt2)};
    const Tangent shared{(ab.base*dt2+bc.base*dt1)*(1.f/(dt1+dt2)),(ab.tip*dt2+bc.tip*dt1)*(1.f/(dt1+dt2))};
    const Curve first{a,b,ab,shared,dt1},second{b,c,shared,bc,dt2};
    for(unsigned i=0;i<=100;++i) {
        const float t=i/100.f;const auto cached=first.Evaluate(t);
        for(float along: {.2f,.88f,.94f,1.f,1.015f}) {
            Check((cached.Position(along)-first.Position(t,along)).Length()<.00001f,"cached curve evaluation changed position");
            Check((cached.Velocity(along)-first.Velocity(t,along)).Length()<.00001f,"cached curve evaluation changed drift velocity");
        }
    }
    const auto incoming=first.Velocity(1.f,.94f),outgoing=second.Velocity(0.f,.94f);
    Check((incoming-outgoing).Length()/incoming.Length()<.004f,"unequal frame durations broke shared world-space tangents");
    Simulation moving;parameters.momentum=.12f;sample(moving,0.f,.016f);sample(moving,.4f,.016f);sample(moving,.8f,.016f);
    const auto snapshot=moving.Particles();moving.Advance(.01f);
    Check(snapshot.size()==moving.Particles().size(),"advance changed live particle count");
    for(std::size_t i=0;i<snapshot.size();++i) {
        const auto& old=snapshot[i];const auto& now=moving.Particles()[i];const float drag=old.ribbon ? 9.f : 3.f,decay=std::exp(-drag*.01f);
        Check((now.position-(old.position+old.velocity*((1.f-decay)/drag))).Length()<.00001f &&
            (now.velocity-old.velocity*decay).Length()<.00001f,"shared damping changed particle motion");
    }
    parameters.momentum=0.f;

    Simulation falling;parameters.gravity=1.f;parameters.momentum=.5f;
    sample(falling,0.f,.016f);sample(falling,.4f,.016f);sample(falling,.8f,.016f);falling.Break();
    const auto born=falling.Particles();auto stepped=falling;
    falling.Advance(.1f);for(unsigned i=0;i<10;++i) {stepped.Advance(.01f);}
    Check(falling.Particles().size()==born.size() && stepped.Particles().size()==born.size(),"physics changed particle count before expiry");
    for(std::size_t i=0;i<born.size();++i) {
        const auto& before=born[i];const auto& now=falling.Particles()[i];
        Check((now.position-stepped.Particles()[i].position).Length()<.002f && (now.velocity-stepped.Particles()[i].velocity).Length()<.002f,"gravity depends on frame subdivision");
        if(before.ribbon) {Check(now.position.z==before.position.z && now.gravity==0.f,"gravity pulled the sword arc down");}
        else {Check(now.position.z<before.position.z && now.velocity.z<before.velocity.z && now.gravity<0.f,"fine particles did not fall naturally");}
    }
    parameters.gravity=0.f;parameters.momentum=0.f;

    const auto count=curved.Particles().size();sample(curved,0.f,.016f);
    curved.Sample({3000,0,0},{3000,100,0},.016f,parameters);curved.Break();
    Check(curved.Particles().size()==count,"teleport drew a connecting arc");
    Simulation noGrains;parameters.density=0.f;sample(noGrains,0.f,.016f);sample(noGrains,.4f,.016f);noGrains.Break();
    Check(!noGrains.Empty(),"two-frame swing or zero grain density hid replacement arc");
    for(const auto& p:noGrains.Particles()) {Check(p.ribbon,"zero grain density emitted points");}
    parameters.density=10.f;parameters.particleLife=.4f;parameters.ribbonLife=.2f;
    for(unsigned i=0;i<4000;++i) {sample(noGrains,i*.08f,.001f);}
    Check(noGrains.Particles().size()<=Simulation::capacity,"particle storage exceeded its cap");
    noGrains.Advance(1.f);Check(noGrains.Empty(),"active history bypassed bounded retention");
    std::vector<std::unique_ptr<Lease>> leases;
    for(unsigned i=0;i<Lease::maximum;++i) {auto lease=std::make_unique<Lease>();Check(lease->Acquire(),"lease unavailable early");leases.push_back(std::move(lease));}
    Lease overflow;Check(!overflow.Acquire(),"instance budget exceeded");leases.clear();Check(overflow.Acquire(),"instance budget did not recover");
    std::cout<<"Continuous curves, separate smooth hold/fade, particle gravity and drag, frame-independent motion and bounded retention passed\n";
}
catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
