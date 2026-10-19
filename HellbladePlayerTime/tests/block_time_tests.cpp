#include "block_time_curve.hpp"
#include <stdexcept>
#include <limits>
#include <iostream>
void Check(bool b) {if(!b) throw std::runtime_error("Block-time curve regression");}
int main() {
    using RedsandBlockTime::Curve;
    for(float seconds:{.6f,1.2f,3.f}) for(float dt:{1.f/144,1.f/60,1.f/30,.1f}) {
        Curve c;Check(c.Start(.2f,seconds));Check(!c.Start(.6f,1));
        float wall=0,previous=1;
        while(c.active) {
            float v=c.Advance(dt);wall+=dt;
            Check(v>=.1999f && v<=1);
            if(wall>.045f && wall<seconds*.70f) Check(std::abs(v-.2f)<.001f);
            if(wall>seconds*.75f) Check(v>=previous-.001f);
            previous=v;Check(wall<seconds+dt*2);
        }
        Check(std::abs(wall-seconds)<=dt+.001f);Check(previous==1);
    }
    Curve c;Check(!c.Start(.2f,0));Check(!c.Start(.2f,std::numeric_limits<float>::quiet_NaN()));
    Check(c.Start(.001f,1));Check(c.minimum==.05f);c.Advance(.5f);c.Reset();Check(!c.active && c.Advance(1)==1);
    std::cout<<"Finite world slowdown, real-time duration at 10-144 FPS, hold/release and reset passed.\n";
}
