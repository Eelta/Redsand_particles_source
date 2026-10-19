#include "lightning_geometry.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
void Check(bool value) {if(!value) throw std::runtime_error("Procedural lightning geometry regression");}
int main() {
    using namespace HellbladeTime;
    Lightning::Geometry geometry;
    VisualFrame frame{{true,.2f,1.5f,437,Source::Attack},{}};
    SessionColors colors{16534};
    auto original=frame.options;
    colors.Apply(frame.session,frame.options);const auto first=frame.options;
    Check(first.brightness==original.brightness && first.density==original.density);
    for(unsigned i=0;i<100;++i) {
        auto same=original;colors.Apply(frame.session,same);
        Check(same.red==first.red && same.green==first.green && same.blue==first.blue);
    }
    const auto randomBeforePause=colors.randomState;
    auto paused=frame.session;paused.active=false;colors.Apply(paused,frame.options);
    Check(colors.randomState==randomBeforePause);
    frame.options=original;frame.options.randomColor=false;colors.Apply(frame.session,frame.options);
    Check(frame.options.red==original.red && frame.options.green==original.green && frame.options.blue==original.blue);
    frame.options=original;colors.Apply(frame.session,frame.options);Check(frame.options.red==first.red);
    auto previous=frame.options;
    for(unsigned i=0;i<1000;++i) {
        ++frame.session.serial;frame.options=original;colors.Apply(frame.session,frame.options);
        const auto& rgb=frame.options;
        Check(std::min({rgb.red,rgb.green,rgb.blue})>=0 && std::min({rgb.red,rgb.green,rgb.blue})<=.45f);
        Check(std::max({rgb.red,rgb.green,rgb.blue})==1);
        Check(std::abs(rgb.red-previous.red)+std::abs(rgb.green-previous.green)+std::abs(rgb.blue-previous.blue)>.6f);
        Check(rgb.brightness==original.brightness && rgb.density==original.density);
        previous=rgb;
    }
    geometry.Build(frame,16.f/9);
    for(const auto& v:geometry.vertices) Check(v.red==frame.options.red && v.green==frame.options.green && v.blue==frame.options.blue);
    frame={{true,.2f,1.5f,437,Source::Attack},{}};
    std::size_t maximum=0;
    for(float aspect:{1.f,16.f/9,32.f/9}) for(float density:{.25f,1.f,3.f}) {
        frame.options.density=density;
        for(unsigned step=0;step<151;++step) {
            frame.session.elapsed=float(step)*.01f;
            geometry.Build(frame,aspect);maximum=std::max(maximum,geometry.vertices.size());
            Check(geometry.vertices.size()<=Lightning::MaxVertices && geometry.vertices.size()%6==0);
            for(const auto& v:geometry.vertices) Check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.energy)&&v.energy>=0);
            if(step==0 || step==150) Check(geometry.vertices.empty());
        }
    }
    frame.options={};frame.session.elapsed=.22f;geometry.Build(frame,16.f/9);auto before=geometry.vertices;
    Check(geometry.paths>70);
    geometry.Build(frame,16.f/9);Check(before.size()==geometry.vertices.size());
    for(unsigned i=0;i<before.size();++i) Check(before[i].x==geometry.vertices[i].x && before[i].y==geometry.vertices[i].y);
    frame.session.elapsed=.24f;geometry.Build(frame,16.f/9);unsigned changed=0;
    for(unsigned i=0;i<std::min(before.size(),geometry.vertices.size());++i)
        changed+=std::abs(before[i].x-geometry.vertices[i].x)+std::abs(before[i].y-geometry.vertices[i].y)>.0001f;
    Check(changed>100);
    frame.session.active=false;geometry.Build(frame,16.f/9);Check(geometry.vertices.empty());
    frame.session.active=true;frame.options.enabled=false;geometry.Build(frame,16.f/9);Check(geometry.vertices.empty());
    frame.options.enabled=true;frame.options.brightness=0;geometry.Build(frame,16.f/9);Check(geometry.vertices.empty());
    frame.options={};frame.session.elapsed=.3f;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<1000;++i) {frame.session.elapsed=.05f+float(i%130)*.01f;geometry.Build(frame,16.f/9);}
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/1000;
    std::cout<<"Stable per-activation RGB, 1000 distinct vivid colors, fixed-color fallback, unchanged brightness/density, shared geometry color; bounded geometry, deformation and fade passed. Maximum vertices="<<maximum<<", default CPU geometry mean="<<ms<<" ms/frame (not a game GPU measurement).\n";
}
