#include "../source/RedsandSwingTrail.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

void Check(bool value,const char* message) { if (!value) { throw std::runtime_error(message); } }
int main() try
{
    using namespace RedsandSwingTrail;
    Parameters parameters;parameters.ribbonLife=.08f;parameters.particleLife=.16f;parameters.width=1.f;parameters.ribbonFade=.18f;parameters.particleFade=.28f;parameters.momentum=0.f;
    Simulation simulation;
    const auto pose=[](float angle) { return Pose{{0,0,0},{std::sin(angle)*100.f,std::cos(angle)*100.f,0}}; };
    const auto sample=[&](Simulation& sim,float angle,float dt) { const auto p=pose(angle);sim.Sample(p.base,p.tip,dt,parameters); };
    const Parameters defaults;
    {
        const auto trace = [&](std::size_t capacity, Vec3 actor) {
            Simulation swing;
            swing.SetRenderCapacity(capacity);
            swing.Seed({{}, {0.f, 100.f, 0.f}});
            swing.Sample({40.f, 0.f, 0.f}, {40.f, 100.f, 0.f}, .04f, defaults, actor);
            std::vector<RibbonEnds> ends;
            std::vector<RibbonRow> rows;
            swing.BuildRibbon({0.f, 0.f, 200.f}, .5f, rows, ends);
            Check(!swing.Empty() && rows.size() > 20, "the native line depended on available particle slots");
            Check((rows.front().center - Vec3{0.f, 100.f, 0.f}).Length() < .001f &&
                (rows.back().center - Vec3{40.f, 100.f, 0.f}).Length() < .001f, "the timed line lost one of its tracked tips");
            return rows;
        };
        const auto limited = trace(0, {});
        const auto full = trace(Simulation::capacity, {});
        const auto walking = trace(0, {40.f, 0.f, 0.f});
        Check(limited.size() == full.size() && full.size() == walking.size(), "scatter density or motion weighting changed line sampling");
        for (std::size_t i = 0; i < full.size(); ++i) {
            Check((limited[i].center - full[i].center).Length() < .001f && limited[i].alpha == full[i].alpha,
                "depleting the scatter pool affected the independent line");
        }
        const auto mid = full.size() / 2;
        Check(walking[mid].alpha > 0.f && walking[mid].alpha < .5f * full[mid].alpha,
            "body translation was mistaken for the strongest part of the blade sweep");
    }
    Parameters fastParameters = defaults;
    fastParameters.width = 1.f;
    Simulation fastSwing;
    fastSwing.Seed(pose(0.f));
    for (const auto [angle, delta] : {std::pair{1.20f, .008f}, {2.00f, .011f}, {2.55f, .008f}}) {
        fastSwing.AdvanceFrame(delta);
        const auto tip = pose(angle);
        fastSwing.Sample(tip.base, tip.tip, delta, fastParameters);
    }
    std::vector<RibbonEnds> ends;
    {
        const auto start = pose(0.f);
        auto finish = pose(1.1f);
        finish.base = Vec3{4.f, 100.f, 3.f} - finish.tip;
        finish.tip = {4.f, 100.f, 3.f};
        Simulation cancelledRotation;
        cancelledRotation.SetRenderCapacity(0);
        cancelledRotation.Seed(start);
        cancelledRotation.Sample(finish.base, finish.tip, .016f, defaults);
        std::vector<RibbonRow> rows;
        cancelledRotation.BuildRibbon({0.f, 0.f, 200.f}, defaults.width, rows, ends);
        Check(rows.size() >= 3 && ends.size() == 1 && ends.front().head.distance < 8.f,
            "a short tip displacement produced a long artificial hook");
        const auto chord = finish.tip - start.tip;
        float previous = -1.f;
        for (const auto& row : rows) {
            const auto offset = row.center - start.tip;
            const float progress = offset.Dot(chord) / chord.Dot(chord);
            Check(progress >= previous && (offset - chord * progress).Length() <= 2.3f,
                "hilt movement made one segment bend backwards or swing wide");
            previous = progress;
        }
        Check((rows.front().center - start.tip).Length() < .001f &&
            (rows.back().center - finish.tip).Length() < .001f, "limiting a hook moved a measured tip");
    }
    {
        Simulation responsive;
        responsive.Seed({{}, {0.f, 100.f, 0.f}});
        responsive.AdvanceFrame(.016f);
        responsive.Sample({60.f, 0.f, 0.f}, {60.f, 100.f, 0.f}, .016f, defaults);
        std::vector<RibbonRow> rows;
        responsive.BuildRibbon({0.f, 0.f, 200.f}, defaults.width, rows, ends);
        const auto leading = [](const auto& row) { return row.center.x >= 55.f && row.center.x <= 57.f; };
        const auto nearTip = std::ranges::find_if(rows, leading);
        Check(nearTip != rows.end() && nearTip->alpha > .55f &&
            (nearTip->right - nearTip->left).Length() > .1f,
            "the first moving frame left a long faint gap behind the blade");
        Check(rows.back().alpha == 0.f && (rows.back().center - Vec3{60.f, 100.f, 0.f}).Length() < .001f,
            "making the head visible lost its pointed physical end");
        const float opacity = nearTip->alpha;
        responsive.EndAttack();
        responsive.BuildRibbon({0.f, 0.f, 200.f}, defaults.width, rows, ends);
        const auto releasedTip = std::ranges::find_if(rows, leading);
        Check(releasedTip != rows.end() && releasedTip->alpha == opacity && releasedTip->coreStrength == 1.f,
            "ending the attack changed the tip taper abruptly");

        const auto widthAt = [](const RibbonRow& row) { return (row.right - row.left).Length(); };
        const auto early = rows.size() / 4, late = rows.size() * 3 / 4;
        const auto releasedRows = rows;
        responsive.Advance(.14f);
        responsive.BuildRibbon({0.f, 0.f, 200.f}, defaults.width, rows, ends);
        Check(rows.size() == releasedRows.size() && rows[early].alpha < .65f * releasedRows[early].alpha &&
            widthAt(rows[early]) < .8f * widthAt(releasedRows[early]) && rows[early].coreStrength < .20f &&
            rows[late].alpha == releasedRows[late].alpha && widthAt(rows[late]) == widthAt(releasedRows[late]),
            "the released ribbon did not feather from the older end along the swing");
        responsive.Advance(.18f);
        responsive.BuildRibbon({0.f, 0.f, 200.f}, defaults.width, rows, ends);
        Check(!rows.empty() && widthAt(rows[rows.size() / 2]) > .1f &&
            std::ranges::all_of(rows, [](const auto& row) { return row.alpha == 0.f && row.coreStrength == 0.f; }),
            "the emissive core survived into the thinnest stage of the ribbon");
        Check(std::ranges::any_of(responsive.Particles(), [&](const auto& particle) {
            return !particle.ribbon && particle.Alpha() > .2f && responsive.PlaceScatterOnRibbon(particle, rows, ends).opacity == 1.f;
        }), "darkening the released ribbon removed its independent round-dot tail");
    }
    for (const float fadeStep : {0.f, .17f}) {
        fastSwing.Advance(fadeStep);
        std::vector<RibbonRow> ribbon;
        fastSwing.BuildRibbon({0.f, 0.f, 200.f}, fastParameters.width, ribbon, ends);
        Check(ribbon.size() > 20, "a fast swing did not produce a connected ribbon");
        Check((ribbon.front().center - pose(0.f).tip).Length() < .001f &&
            (ribbon.back().center - pose(2.55f).tip).Length() < .001f && ends.front().head.distance > 240.f,
            "a long swing lost one of its measured endpoints");
        const auto widthAt = [](const RibbonRow& row) { return (row.left - row.right).Length(); };
        Check(widthAt(ribbon.front()) < .001f && widthAt(ribbon.back()) < .001f, "both ribbon tips must collapse to a point");
        Check(widthAt(ribbon[1]) > .001f && widthAt(ribbon[ribbon.size() - 2]) > .001f, "the ribbon is missing its tapered ends");
        for (std::size_t i = 1; i + 1 < ribbon.size(); ++i) {
            Check(widthAt(ribbon[i]) > .001f && ribbon[i].alpha > 0.f, "a frame seam broke the opaque part of the ribbon");
        }
    }
    fastSwing.Break();
    fastSwing.Seed(pose(-.77f));
    const auto restarted = pose(-.25f);
    fastSwing.Sample(restarted.base, restarted.tip, .016f, fastParameters);
    std::vector<RibbonRow> split;
    fastSwing.BuildRibbon({0.f, 0.f, 200.f}, .5f, split, ends);
    const auto degenerate = [](const auto& row) { return (row.left - row.right).Length() < .001f && row.alpha == 0.f; };
    Check(std::adjacent_find(split.begin(), split.end(), [&](const auto& a, const auto& b) {
        return degenerate(a) && degenerate(b) && (a.center - b.center).Length() > 40.f;
    }) != split.end(), "a broken path produced a visible connector across separate strokes");
    unsigned fallbackPoints = 0;
    const auto fallbackCount = SampleFallbackRibbon(split, 2048, [&](Vec3 position, float radius, float alpha) {
        Check(std::abs(position.Length() - 100.f) < 5.f && (position.x >= -.01f || position.x < -20.f),
            "fallback distorted the swing or connected the gap between separate strokes");
        Check(radius > 0.f && alpha > 0.f, "fallback emitted an invisible point");
        ++fallbackPoints;
    });
    Check(fallbackCount == fallbackPoints && fallbackCount > 1400 && fallbackCount <= 2048,
        "an unavailable mesh removed the whole visible centerline or exceeded the fallback budget");
    Check(ends.size() == 2, "independent strokes must publish independent endpoint limits");
    {
        std::vector<RibbonTracePoint> measured;
        const auto warped = [](float t) {
            return Vec3{-55.f + 110.f * t, 165.f + 22.f * std::sin(9.f * t), 22.f * std::sin(8.f * t) + 30.f * std::sin(3.14159f * t)};
        };
        for (unsigned i = 0; i <= 60; ++i) {
            auto point = warped(i / 60.f);
            point.z += (i % 2 ? 1.f : -1.f) * 1.1f;
            measured.push_back({2.f * i, point});
        }
        float noisy = 0.f, smoothed = 0.f;
        RibbonSpline spline;
        spline.Set(measured, 0.f, 120.f, 8.f);
        const auto smooth = [&](float at) { return spline.At(at).position; };
        for (float at = 8.f; at < 112.f; at += 2.f) {
            const auto raw = RibbonTracePosition(measured, at);
            const auto center = smooth(at);
            Check((center - raw).Length() <= 4.5f, "the local spline drifted away from measured blade travel");
            noisy += (RibbonTracePosition(measured, at - 2.f) + RibbonTracePosition(measured, at + 2.f) - raw * 2.f).Length();
            smoothed += (smooth(at - 2.f) + smooth(at + 2.f) - center * 2.f).Length();
        }
        Check(smoothed < .45f * noisy, "the local spline did not smooth inconsistent sword samples");
        Check((smooth(0.f) - measured.front().position).Length() < .001f &&
            (smooth(120.f) - measured.back().position).Length() < .001f, "the spline shifted one of the physical tips");

        const std::vector<RibbonTracePoint> corner{{0.f, {}}, {24.f, {24.f, 0.f, 0.f}}, {56.f, {24.f, 32.f, 0.f}}};
        spline.Set(corner, 0.f, 56.f, 8.f);
        for (float at = 4.f; at < 56.f; at += 4.f) {
            const auto left = spline.At(at - .01f);
            const auto right = spline.At(at + .01f);
            Check(left.tangent.Dot(right.tangent) > .9999f * left.tangent.Length() * right.tangent.Length() &&
                (left.position - right.position).Length() < .03f,
                "a measured corner or spline joint left a kink in the rendered path");
        }
        Check((spline.At(24.f).position - corner[1].position).Length() < 3.f &&
            spline.At(24.f).tangent.x > 0.f && spline.At(24.f).tangent.y > 0.f,
            "rounding a sharp corner moved the curve off the nearby blade path");

        const std::vector<RibbonTracePoint> sidewaysKink{
            {0.f, {}}, {8.f, {8.f, 0.f, 0.f}}, {16.f, {16.f, 0.f, 0.f}}, {24.f, {22.f, 4.5f, 0.f}},
            {32.f, {30.f, 0.f, 0.f}}, {40.f, {38.f, 0.f, 0.f}}, {48.f, {46.f, 0.f, 0.f}}};
        spline.Set(sidewaysKink, 0.f, 48.f, 8.f);
        Check(std::abs(spline.At(24.f).position.y) < .15f && spline.BypassedDetours() == 1,
            "a brief sideways kink survived even though the sweep before and after it agreed");
        Check((spline.At(0.f).position - sidewaysKink.front().position).Length() < .001f &&
            (spline.At(48.f).position - sidewaysKink.back().position).Length() < .001f,
            "removing the sideways kink moved a measured tip");
        const std::vector<RibbonTracePoint> twoPointKink{
            {0.f, {}}, {8.f, {8.f, 0.f, 0.f}}, {16.f, {16.f, 0.f, 0.f}}, {24.f, {23.f, 3.4f, 0.f}},
            {32.f, {31.f, 3.4f, 0.f}}, {40.f, {38.f, 0.f, 0.f}}, {48.f, {46.f, 0.f, 0.f}}, {56.f, {54.f, 0.f, 0.f}}};
        spline.Set(twoPointKink, 0.f, 56.f, 8.f);
        Check(spline.BypassedDetours() == 1 && std::abs(spline.At(28.f).position.y) < .15f &&
            (spline.At(56.f).position - twoPointKink.back().position).Length() < .001f,
            "a short kink spanning two samples survived even though the approach and departure agreed");

        std::vector<RibbonTracePoint> alternating;
        for (unsigned i = 0; i < 9; ++i) {
            const float x = 8.f * i;
            alternating.push_back({x, {x, i % 2 ? 1.7f : -1.7f, 0.f}});
        }
        alternating.push_back({69.f, {69.f, 0.f, 0.f}});
        spline.Set(alternating, 0.f, 69.f, 8.f);
        float peakSideSlope = 0.f;
        for (float at = 12.f; at <= 56.f; at += .25f) {
            const auto point = spline.At(at);
            Check(point.tangent.x > .8f, "smoothing small kinks interrupted forward blade travel");
            peakSideSlope = std::max(peakSideSlope, std::abs(point.tangent.y / point.tangent.x));
        }
        Check(peakSideSlope < .12f, "small neighboring kinks still read as a zigzag on the wide portion of the ribbon");
        Check((spline.At(0.f).position - alternating.front().position).Length() < .001f &&
            (spline.At(69.f).position - alternating.back().position).Length() < .001f,
            "smoothing unevenly spaced samples displaced a tip");

        const std::vector<RibbonTracePoint> endKinks{
            {0.f, {}}, {4.f, {-2.f, 1.f, 0.f}}, {8.f, {1.f, 2.f, 0.f}}, {12.f, {6.f, 3.f, 0.f}},
            {16.f, {10.f, 3.5f, 0.f}}, {24.f, {18.f, 5.f, 0.f}}, {36.f, {30.f, 7.f, 0.f}},
            {48.f, {42.f, 5.f, 0.f}}, {56.f, {50.f, 3.5f, 0.f}}, {60.f, {54.f, 3.f, 0.f}},
            {64.f, {59.f, 2.f, 0.f}}, {68.f, {62.f, 1.f, 0.f}}, {72.f, {60.f, 0.f, 0.f}}};
        spline.Set(endKinks, 0.f, 72.f, 8.f);
        Check(spline.RoundedEnds() == 2 && (spline.At(0.f).position - endKinks.front().position).Length() < .001f &&
            (spline.At(72.f).position - endKinks.back().position).Length() < .001f,
            "rounding the two ends omitted a measured tip or left the endpoint kink untouched");
        for (float at = 0.f; at <= 72.f; at += .5f) {
            Check(spline.At(at).tangent.x > 0.f, "the rounded tip still bent back on the main sweep");
        }
        for (float join : {12.8f, 59.2f}) {
            const auto left = spline.At(join - .005f);
            const auto right = spline.At(join + .005f);
            Check(left.tangent.Dot(right.tangent) > .9999f * left.tangent.Length() * right.tangent.Length(),
                "the rounded tip introduced a new corner at its join");
        }

        Simulation irregular;
        const Vec3 hilt{0.f, 100.f, 0.f};
        irregular.Seed({hilt, warped(0.f)});
        for (unsigned i = 1; i <= 40; ++i) { irregular.Sample(hilt, warped(i / 40.f), .007f, defaults); }
        std::vector<RibbonRow> fitted, otherView;
        std::vector<RibbonEnds> limits, otherEnds;
        irregular.BuildRibbon({}, .5f, fitted, limits);
        irregular.BuildRibbon({-120.f, 260.f, 40.f}, .5f, otherView, otherEnds);
        Check(fitted.size() > 16 && otherView.size() == fitted.size() && limits.size() == 1, "the measured sweep was lost");
        for (std::size_t i = 0; i < fitted.size(); ++i) {
            Check((fitted[i].center - otherView[i].center).Length() < .001f, "the local spline turned with the camera");
        }
        const float activeEnd = limits.front().head.distance;
        Check((fitted.front().center - warped(0.f)).Length() < .001f && (fitted.back().center - warped(1.f)).Length() < .001f,
            "the active swing did not retain the measured windup and follow-through");
        irregular.EndAttack();
        for (const float delta : {0.f, .03f, .05f, .18f}) {
            irregular.Advance(delta);
            irregular.BuildRibbon({}, .5f, fitted, limits);
            Check(limits.front().head.distance == activeEnd && limits.front().tail.distance == 0.f &&
                (fitted.front().center - warped(0.f)).Length() < .001f && (fitted.back().center - warped(1.f)).Length() < .001f,
                "ending or fading the swing cropped a still-live tip");
        }
    }
    {
        std::vector<RibbonTracePoint> hookedPath;
        const auto append = [&](Vec3 point) {
            const float distance = hookedPath.empty() ? 0.f : hookedPath.back().distance + (point - hookedPath.back().position).Length();
            hookedPath.push_back({distance, point});
        };
        const auto circle = [](float angle) { return Vec3{80.f * std::sin(angle), 80.f * std::cos(angle), 0.f}; };
        const auto hook = [&](float angle, unsigned start) {
            const Vec3 forward{std::cos(angle), -std::sin(angle), 0.f};
            const Vec3 normal{-forward.y, forward.x, 0.f};
            for (unsigned i = start; i <= 24; ++i) {
                const float turn = static_cast<float>(i) * (.26179939f);
                append(circle(angle) + forward * (6.f * std::sin(turn)) + normal * (6.f * (1.f - std::cos(turn))));
            }
        };
        hook(-1.f, 0);
        for (unsigned i = 1; i <= 80; ++i) {
            append(circle(-1.f + .025f * i));
            if (i == 40) { hook(0.f, 1); }
        }
        hook(1.f, 1);
        const auto rawEnds = RibbonTraceEnds(0, hookedPath);
        Check(rawEnds.tail.distance == 0.f && rawEnds.head.distance == hookedPath.back().distance,
            "a curved windup or recovery shortened the physical stroke");

        Simulation hooked;
        hooked.Seed({{}, hookedPath.front().position});
        for (std::size_t i = 1; i < hookedPath.size(); ++i) {
            hooked.Sample({}, hookedPath[i].position, .006f, defaults);
        }
        hooked.EndAttack();
        hooked.Advance(.065f);
        std::vector<RibbonRow> full;
        std::vector<RibbonEnds> fullEnds;
        hooked.BuildRibbon({0.f, 0.f, 200.f}, 1.f, full, fullEnds);
        Check(full.size() > 25 && fullEnds.size() == 1 && degenerate(full.front()) && degenerate(full.back()),
            "retaining the full stroke flattened a tip or lost the arc");
        Check((full.front().center - hookedPath.front().position).Length() < .01f &&
            (full.back().center - hookedPath.back().position).Length() < .01f &&
            (full.front().center - fullEnds.front().tail.position).Length() < .01f &&
            (full.back().center - fullEnds.front().head.position).Length() < .01f,
            "mesh and scatter lost their measured endpoint positions");
        Check(fullEnds.front().detours == 3, "tight returns at the start, middle and end remained in the ribbon");
        for (std::size_t i = 1; i < full.size(); ++i) {
            const auto a = full[i - 1].center;
            const auto b = full[i].center;
            Check(std::atan2(a.x, a.y) <= std::atan2(b.x, b.y) + .001f && std::abs(b.Length() - 80.f) < 4.f,
                "a collapsed hook still bent backwards or pulled the broad sweep off the blade path");
        }
    }
    {
        const auto lightOutput = [](PointLighting light) { return light.materialBrightness * light.grainOpacity; };
        const auto hidden = ResolvePointLighting(0.f, 6.f, false);
        const auto faint = ResolvePointLighting(.5f, 6.f, false);
        const auto bright = ResolvePointLighting(3.f, 6.f, false);
        const auto arcOff = ResolvePointLighting(3.f, 0.f, false);
        Check(lightOutput(hidden) == 0.f && faint.grainOpacity > 0.f && faint.grainOpacity < .5f &&
            lightOutput(bright) > 8.f * lightOutput(faint) && lightOutput(arcOff) == lightOutput(bright) &&
            bright.materialBrightness == arcOff.materialBrightness && bright.fallbackOpacity == 0.f,
            "arc brightness affected real round-dot lighting or the low end failed to dim");
        const auto fallback = ResolvePointLighting(3.f, 6.f, true);
        const auto faintFallback = ResolvePointLighting(.5f, 6.f, true);
        Check(std::abs(lightOutput(fallback) - lightOutput(bright)) < .001f &&
            std::abs(lightOutput(faintFallback) - lightOutput(faint)) < .001f &&
            fallback.materialBrightness * fallback.fallbackOpacity == 6.f &&
            faintFallback.materialBrightness * faintFallback.fallbackOpacity == 6.f,
            "changing one effect altered the other in the shared point fallback");
    }
    for (const float length : {24.f, 60.f, 180.f, 320.f}) {
        Simulation slash;
        slash.Seed({{}, {0.f, 100.f, 0.f}});
        const auto frames = static_cast<unsigned>(std::ceil(length / 80.f));
        for (unsigned frame = 1; frame <= frames; ++frame) {
            const float x = length * frame / frames;
            slash.AdvanceFrame(.016f);
            slash.Sample({x, 0.f, 0.f}, {x, 100.f, 0.f}, .016f, fastParameters);
        }
        std::vector<RibbonRow> rows;
        slash.BuildRibbon({length * .5f, 100.f, 200.f}, .3f, rows, ends);
        const auto widthAt = [&](float progress) {
            const auto& row = rows[std::lround(progress * (rows.size() - 1))];
            return (row.right - row.left).Length();
        };
        Check(rows.size() >= 16 && widthAt(0.f) < .001f && widthAt(1.f) < .001f,
            "the slash lost a pointed endpoint");
        const auto middle = widthAt(.5f);
        Check(middle > 1.95f && middle < 4.25f && widthAt(.10f) < .20f * middle && widthAt(.90f) < .20f * middle &&
            widthAt(.25f) < .68f * middle && widthAt(.75f) < .68f * middle,
            "the visible mesh lacks a narrow body and two long tapers at the current width setting");
        slash.BuildRibbon({length * .5f, 100.f, 200.f}, .6f, rows, ends);
        Check(widthAt(.5f) > 1.9f * middle, "the thickness slider stopped scaling the slash body");
    }
    std::vector<RibbonRow> straight;
    for (unsigned i = 0; i <= 100; ++i) {
        const float envelope = RibbonEnvelope(i / 100.f);
        const float width = .575f * envelope * envelope;
        const float x = static_cast<float>(i);
        straight.push_back({{x, -width, 0.f}, {x, 0.f, 0.f}, {x, width, 0.f}, width > 0.f ? 1.f : 0.f});
    }
    unsigned tipSamples = 0, bodySamples = 0;
    SampleFallbackRibbon(straight, 2048, [&](Vec3 position, float radius, float) {
        const float fromTip = std::min(position.x, 100.f - position.x);
        if (fromTip < 5.f) {
            Check(radius < .05f, "the fallback widened one of the final tips");
            ++tipSamples;
        }
        if (position.x >= 45.f && position.x < 55.f) { ++bodySamples; }
    });
    Check(bodySamples > 0 && tipSamples > 1.6f * bodySamples, "the fallback did not sample both fine tips more densely");
    {
        auto config = defaults;
        config.ribbonLife = .03f;
        config.ribbonFade = .03f;
        config.momentum = 1.f;
        Simulation released;
        released.Seed({{}, {0.f, 100.f, 0.f}});
        released.AdvanceFrame(.04f);
        released.Sample({80.f, 0.f, 0.f}, {80.f, 100.f, 0.f}, .04f, config);
        std::vector<RibbonRow> rows;
        std::vector<RibbonEnds> limits;
        released.BuildRibbon({0.f, 0.f, 200.f}, config.width, rows, limits);
        Check(rows.size() > 2 && limits.size() == 1, "the released trajectory needs a visible tip before fading");
        for (const auto& grain : released.Particles()) {
            if (!grain.ribbon) {
                const auto placed = released.PlaceScatterOnRibbon(grain, rows, limits);
                Check(std::abs(placed.position.y - 100.f) < .001f && std::abs(placed.position.z) < .001f,
                    "a newly emitted grain separated from the blade path");
            }
        }
        released.EndAttack();
        released.Advance(.20f);
        released.BuildRibbon({0.f, 0.f, 200.f}, config.width, rows, limits);
        Check(rows.empty() && limits.size() == 1, "the main line should expire before the grains");
        float furthest = 0.f;
        bool swayedLeft = false, swayedRight = false, fluttered = false;
        for (const auto& grain : released.Particles()) {
            const auto placed = released.PlaceScatterOnRibbon(grain, rows, limits);
            Check(!grain.ribbon && grain.Alpha() > 0.f && placed.opacity == 1.f,
                "the tip cut off a live grain");
            Check(placed.position.z < -.3f && placed.position.z > -4.f, "a residual grain did not settle gently");
            swayedLeft |= placed.position.y < 99.8f;
            swayedRight |= placed.position.y > 100.2f;
            auto straightFall = grain;
            straightFall.flutterOffset = {};
            const auto withoutFlutter = released.PlaceScatterOnRibbon(straightFall, rows, limits).position;
            fluttered |= std::abs(placed.position.y - withoutFlutter.y) > .8f;
            furthest = std::max(furthest, placed.position.x - limits.front().head.position.x);
        }
        Check(furthest > 12.f, "the particles were still held at the end of the stroke");
        Check(swayedLeft && swayedRight && fluttered, "the residual grains still move as one rigid row");
        Simulation vertical;
        vertical.Seed({{}, {0.f, 100.f, 0.f}});
        vertical.Sample({0.f, 0.f, 80.f}, {0.f, 100.f, 80.f}, .04f, config);
        Check(std::ranges::any_of(vertical.Particles(), [](const auto& grain) {
            return !grain.ribbon && std::hypot(grain.flutterOffset.x, grain.flutterOffset.y) > .8f;
        }), "a vertical swing had no sideways flutter in its tail");
        Particle probe;
        probe.stroke = limits.front().stroke;
        probe.strokeDistance = limits.front().head.distance - 20.f;
        probe.emissionCenter = {60.f, 100.f, 0.f};
        probe.swingDirection = {1.f, 0.f, 0.f};
        const auto place = [&](float drift) {
            probe.position = probe.emissionCenter + Vec3{drift, 0.f, 0.f};
            return released.PlaceScatterOnRibbon(probe, rows, limits);
        };
        const auto start = place(0.f);
        const auto last = place(19.99f);
        const auto atTip = place(20.f);
        const auto beyond = place(36.f);
        Check(std::abs((atTip.position.x - start.position.x) - 20.f) < .001f &&
            std::abs((atTip.position.x - last.position.x) - .01f) < .001f &&
            std::abs((beyond.position.x - atTip.position.x) - 16.f) < .001f,
            "the endpoint changed the drift speed");
        Check(last.opacity == 1.f && atTip.opacity == 1.f && beyond.opacity == 1.f,
            "crossing the endpoint triggered an extra fade");
        Check(released.PlaceScatterOnRibbon(Particle{}, rows, {}).opacity == 0.f, "a grain with no matching stroke was displayed");

        Simulation curvedEnd;
        curvedEnd.Seed(pose(0.f));
        const auto tip = pose(.9f);
        curvedEnd.Sample(tip.base, tip.tip, .04f, config);
        curvedEnd.BuildRibbon({0.f, 0.f, 200.f}, config.width, rows, limits);
        Check(rows.size() > 2 && limits.size() == 1, "curved sweep was not rendered");
        const auto lastSegment = rows.back().center - rows[rows.size() - 2].center;
        const auto renderDirection = lastSegment * (1.f / lastSegment.Length());
        probe.stroke = limits.front().stroke;
        probe.strokeDistance = limits.front().head.distance;
        probe.position = probe.emissionCenter + probe.swingDirection * 10.f;
        probe.sideOffset = 2.f;
        const auto beforeFade = curvedEnd.PlaceScatterOnRibbon(probe, rows, limits);
        Check(std::abs((beforeFade.position - limits.front().head.position).Dot(renderDirection) - 10.f) < .01f,
            "the continuation did not follow the rendered final segment");
        curvedEnd.EndAttack();
        curvedEnd.Advance(.20f);
        curvedEnd.BuildRibbon({0.f, 0.f, 200.f}, config.width, rows, limits);
        const auto afterFade = curvedEnd.PlaceScatterOnRibbon(probe, rows, limits);
        Check(rows.empty() && (afterFade.position - beforeFade.position).Length() < .001f && afterFade.opacity == 1.f,
            "the outgoing direction or width jumped after the arc faded");
    }
    {
        Simulation shortSwing;
        shortSwing.SetRenderCapacity(2);
        shortSwing.Seed(pose(0.f));
        const auto tip = pose(.06f);
        shortSwing.Sample(tip.base, tip.tip, .016f, defaults);
        std::vector<RibbonRow> shortRows;
        std::vector<RibbonEnds> shortEnds;
        shortSwing.BuildRibbon({0.f, 0.f, 200.f}, .5f, shortRows, shortEnds);
        const auto spark = std::ranges::find_if(shortSwing.Particles(), [](const auto& point) { return !point.ribbon; });
        Check(spark != shortSwing.Particles().end() && std::abs(spark->distanceFraction - .5f) < .001f &&
            shortSwing.PlaceScatterOnRibbon(*spark, shortRows, shortEnds).opacity > .2f,
            "the only scatter particle of a short stroke was born at a fully faded tip");
    }
    const auto layerOrder = [&](bool arcFirst) {
        Parameters durations;
        durations.ribbonLife = arcFirst ? .03f : .4f;
        durations.ribbonFade = arcFirst ? .06f : .3f;
        durations.particleLife = arcFirst ? .4f : .05f;
        durations.particleFade = arcFirst ? .3f : .08f;
        durations.momentum = 0.f;
        Simulation independent;
        independent.Seed(pose(0.f));
        const auto tip = pose(.7f);
        independent.Sample(tip.base, tip.tip, .016f, durations);
        independent.EndAttack();
        independent.Advance(.2f);
        std::vector<RibbonRow> visibleArc;
        std::vector<RibbonEnds> retainedEnds;
        independent.BuildRibbon({0.f, 0.f, 200.f}, .5f, visibleArc, retainedEnds);
        const bool visibleScatter = std::ranges::any_of(independent.Particles(), [&](const Particle& p) {
            return !p.ribbon && p.Alpha() * independent.PlaceScatterOnRibbon(p, visibleArc, retainedEnds).opacity > .1f;
        });
        Check(arcFirst ? visibleArc.empty() && visibleScatter : !visibleArc.empty() && !visibleScatter,
            "a layer followed the other layer's hold/fade settings");
        independent.Advance(1.f);
        independent.BuildRibbon({0.f, 0.f, 200.f}, .5f, visibleArc, retainedEnds);
        Check(independent.Empty() && retainedEnds.empty(), "expired strokes retained endpoint history");
    };
    layerOrder(true);
    layerOrder(false);
    Parameters scatterParameters = defaults;
    scatterParameters.momentum = 0.f;
    const auto scatterAlongArc = [&](float density) {
        Simulation scatter;
        scatterParameters.density = density;
        scatter.Seed(pose(0.f));
        const auto tip = pose(.85f);
        scatter.Sample(tip.base, tip.tip, .016f, scatterParameters);
        std::vector<RibbonRow> rows;
        std::vector<RibbonEnds> limits;
        scatter.BuildRibbon({0.f, 0.f, 200.f}, scatterParameters.width, rows, limits);
        unsigned count = 0, late = 0;
        for (const auto& grain : scatter.Particles()) {
            if (grain.ribbon) { continue; }
            const float radius = std::hypot(grain.position.x, grain.position.y);
            Check(std::abs(radius - 100.f) < .001f, "spacing variation is still pushing grains to either side of the swing");
            const auto placed = scatter.PlaceScatterOnRibbon(grain, rows, limits);
            float gap = 100.f;
            for (std::size_t i = 1; i < rows.size(); ++i) {
                const auto edge = rows[i].center - rows[i - 1].center;
                const auto offset = placed.position - rows[i - 1].center;
                const float t = std::clamp(offset.Dot(edge) / std::max(.0001f, edge.Dot(edge)), 0.f, 1.f);
                gap = std::min(gap, (offset - edge * t).Length());
            }
            Check(placed.opacity == 1.f && gap < .001f,
                "a visible grain did not follow the exact rendered centerline");
            ++count;
            late += grain.distanceFraction > .75f;
        }
        return std::array{count, late};
    };
    const auto dense = scatterAlongArc(1.f);
    const auto sparse = scatterAlongArc(.35f);
    Check(dense[0] >= 36 && dense[1] >= 8, "the single row did not reach the end of the swing");
    Check(dense[0] > sparse[0], "scatter ignored the player's density control");
    Simulation continuous;
    continuous.Seed(pose(2.50f));
    float angle = 2.50f;
    for (unsigned frame = 0; frame < 240; ++frame) {
        continuous.AdvanceFrame(.008f);
        angle += frame < 80 ? -.25f : .29f;
        const auto current = pose(angle);
        continuous.Sample(current.base, current.tip, .008f, defaults);
        Check(std::ranges::any_of(continuous.Particles(), [&](const Particle& particle) {
            return particle.ribbon && particle.age == 0.f && (particle.position - current.tip).Length() < .001f;
        }), "a long or reversing stroke lost its current tip");
        Check(continuous.Particles().size() < Simulation::capacity, "continuous movement exhausted the vertex buffer");
    }
    continuous.Advance(.85f);
    const auto idle = pose(angle);
    for (int n = 0; n < 4; ++n) { continuous.Sample(idle.base, idle.tip, .016f, defaults); }
    Check(continuous.Empty(), "a stationary weapon emitted a trail");
    const auto resumed = pose(angle + .0002f);
    continuous.Sample(resumed.base, resumed.tip, .016f, defaults);
    Check(continuous.Particles().size() == 2 && continuous.Particles()[0].ribbon &&
        !continuous.Particles()[1].ribbon && std::abs(continuous.Particles()[1].distanceFraction - .5f) < .001f,
        "the first moving frame after a pause lost its arc or spark");
    const auto firstAlpha = continuous.Particles().front().Alpha();
    continuous.EndAttack();
    continuous.AdvanceFrame(.5f);
    Check(!continuous.Empty() && continuous.Particles().front().Alpha() == firstAlpha,
        "a stopped swing cut its fade on a stalled frame");
    for (int n = 0; n < 55; ++n) { continuous.AdvanceFrame(.016f); }
    Check(continuous.Empty(), "a stopped swing did not finish its particle fade");
    sample(simulation,0.f,.016f);sample(simulation,.4f,.016f);
    Check(!simulation.Empty(),"current movement waited for a future frame");
    float nearest=1000.f;for(const auto& p:simulation.Particles())if(p.ribbon && p.intensity==.85f)nearest=std::min(nearest,(p.position-pose(.4f).tip).Length());
    Check(nearest<.001f,"visible arc head trails behind the current weapon pose");
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
    Check(simulation.Particles().front().age > .12f,"the visible trail did not retain its earlier samples");
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
            Check(std::abs(p.position.Length()-100.f)<.03f,"curve collapsed inward into endpoint chords");middle.push_back(p.position);
        }
    }
    for(std::size_t i=2;i<middle.size();++i) {
        const auto a=middle[i-1]-middle[i-2],b=middle[i]-middle[i-1];
        Check(a.Dot(b)/(a.Length()*b.Length())>.995f,"multi-frame circular arc has a visible corner");
    }
    const Pose a{{0,0,0},{0,100,0}}, b{{2,1,0},{52,87.6025f,0}}, c{{5,3,0},{31,71,35}};
    const Curve first{a, b, .02f};
    for(unsigned i=0;i<=100;++i) {
        const float t=i/100.f;const auto cached=first.Evaluate(t);
        for(float along: {.2f,.88f,.94f,1.f,1.015f}) {
            Check((cached.Position(along)-first.Position(t,along)).Length()<.00001f,"cached curve evaluation changed position");
            Check((cached.Velocity(along)-first.Velocity(t,along)).Length()<.00001f,"cached curve evaluation changed drift velocity");
        }
    }
    {
        Simulation unevenSwing;
        unevenSwing.Seed(a);
        unevenSwing.AdvanceFrame(.02f);
        unevenSwing.Sample(b.base, b.tip, .02f, defaults);
        unevenSwing.AdvanceFrame(.04f);
        const auto published = unevenSwing.Particles();
        unevenSwing.Sample(c.base, c.tip, .04f, defaults);
        Check(!published.empty() && published.size() < unevenSwing.Particles().size(), "the unequal-frame fixture did not publish both steps");
        for (std::size_t i = 0; i < published.size(); ++i) {
            const auto& current = unevenSwing.Particles()[i];
            Check((current.position - published[i].position).Length() < .001f &&
                (current.emissionCenter - published[i].emissionCenter).Length() < .001f,
                "the next pose moved already published particles");
        }
    }
    Simulation moving;parameters.momentum=.12f;sample(moving,0.f,.016f);sample(moving,.4f,.016f);sample(moving,.8f,.016f);
    const auto snapshot=moving.Particles();moving.Advance(.01f);
    Check(snapshot.size()==moving.Particles().size(),"advance changed live particle count");
    for(std::size_t i=0;i<snapshot.size();++i) {
        const auto& old=snapshot[i];const auto& now=moving.Particles()[i];const float drag=3.f,decay=std::exp(-drag*.01f);
        Check((now.position-(old.position+old.velocity*((1.f-decay)/drag))).Length()<.00001f &&
            (now.velocity-old.velocity*decay).Length()<.00001f,"shared damping changed particle motion");
    }
    parameters.momentum=0.f;

    const auto velocityAtSpeed = [&](float speed) {
        auto config = defaults;
        config.momentum = speed;
        Simulation swing;
        swing.Seed({{}, {0.f, 100.f, 0.f}});
        swing.Sample({32.f, 0.f, 0.f}, {32.f, 100.f, 0.f}, .016f, config);
        const auto grain = std::ranges::find_if(swing.Particles(), [](const auto& p) { return !p.ribbon; });
        Check(grain != swing.Particles().end(), "the speed fixture did not emit a grain");
        return grain->velocity.x;
    };
    Check(velocityAtSpeed(0.f) == 0.f && std::abs(velocityAtSpeed(.5f) * 2.f - velocityAtSpeed(1.f)) < .001f &&
        std::abs(velocityAtSpeed(1.f) * 2.f - velocityAtSpeed(2.f)) < .001f,
        "the single speed control did not scale motion through its range");

    const auto grainDistances = [&](float density, unsigned frames) {
        auto config = defaults;
        config.density = density;
        Simulation swing;
        swing.Seed({{}, {0.f, 100.f, 0.f}});
        for (unsigned frame = 1; frame <= frames; ++frame) {
            const float x = 112.f * frame / frames;
            swing.Advance(.032f / frames);
            swing.Sample({x, 0.f, 0.f}, {x, 100.f, 0.f}, .032f / frames, config);
        }
        std::vector<float> distances;
        for (const auto& grain : swing.Particles()) {
            if (!grain.ribbon) { distances.push_back(grain.strokeDistance); }
        }
        return distances;
    };
    const auto near = grainDistances(.15f, 1);
    const auto far = grainDistances(.025f, 1);
    const auto splitGrains = grainDistances(.025f, 3);
    Check(near.size() > 3 * far.size() && far.size() >= 3 && far[1] - far[0] > 3 * (near[1] - near[0]),
        "the density control did not change the count and spacing together");
    Check(far.size() == splitGrains.size(), "the spacing interval changed the grain count across frames");
    for (unsigned i = 0; i < far.size(); ++i) {
        Check(std::abs(far[i] - splitGrains[i]) < .001f, "grain birth spacing reset at a frame boundary");
    }
    {
        auto config = defaults;
        config.particleWidth = 6.f;
        config.momentum = 0.f;
        Simulation swing;
        swing.Seed({{}, {0.f, 100.f, 0.f}});
        swing.Sample({80.f, 0.f, 0.f}, {80.f, 100.f, 0.f}, .032f, config);
        std::vector<RibbonRow> rows;
        std::vector<RibbonEnds> ends;
        swing.BuildRibbon({0.f, 0.f, 200.f}, config.width, rows, ends);
        std::vector<RibbonRow> hiddenRows;
        std::vector<RibbonEnds> hiddenEnds;
        swing.BuildRibbon({0.f, 0.f, 200.f}, 0.f, hiddenRows, hiddenEnds);
        Check(SampleFallbackRibbon(hiddenRows, 2048, [](Vec3, float, float) {}) == 0,
            "a zero-width arc still emitted its fallback");
        bool left = false, right = false;
        for (const auto& grain : swing.Particles()) {
            if (grain.ribbon) { continue; }
            auto centered = grain;
            centered.sideOffset = 0.f;
            const auto origin = swing.PlaceScatterOnRibbon(centered, rows, ends);
            const auto placed = swing.PlaceScatterOnRibbon(grain, rows, ends);
            const auto withArcHidden = swing.PlaceScatterOnRibbon(grain, hiddenRows, hiddenEnds);
            Check((placed.position - withArcHidden.position).Length() < .001f && placed.opacity == withArcHidden.opacity,
                "hiding the arc changed the independent round-dot path");
            const auto offset = placed.position - origin.position;
            Check(std::abs(offset.Length() - std::abs(grain.sideOffset)) < .001f &&
                std::abs(offset.x) < .001f && offset.Length() <= 3.001f,
                "particle width changed arc distance or exceeded the selected width");
            left |= grain.sideOffset < -.5f;
            right |= grain.sideOffset > .5f;
        }
        Check(left && right, "the independent width did not spread grains to both sides");
    }

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
    std::cout<<"Continuous curves, separate smooth hold/fade, one speed control and bounded drift, frame-independent motion and bounded retention passed\n";
}
catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
